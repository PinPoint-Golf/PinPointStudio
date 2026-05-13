/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_SPINNAKER
// Include Spinnaker headers before Qt to avoid macro conflicts (like 'signals')
#include "SpinnakerPlatform.h"
#undef SPINNAKER_DEPRECATED_CLASS
#define SPINNAKER_DEPRECATED_CLASS(msg) class SPINNAKER_API __declspec(deprecated(msg))
#include "Spinnaker.h"
#include "SpinGenApi/SpinnakerGenApi.h"
#endif

#include "VideoInputSpinnaker.h"
#include "raw_video_frame.h"
#include <QVideoFrame>
#include "pp_debug.h"
#include <QtConcurrent>

#ifdef HAVE_SPINNAKER
using namespace Spinnaker;
using namespace Spinnaker::GenApi;
using namespace Spinnaker::GenICam;
#endif

VideoInputSpinnaker::VideoInputSpinnaker(QObject *parent)
    : VideoInputBase(parent)
{
}

VideoInputSpinnaker::~VideoInputSpinnaker()
{
    stop();
}

bool VideoInputSpinnaker::start(const QString &deviceId)
{
    stop();
    m_abort = false;

#ifdef HAVE_SPINNAKER
    try {
        SystemPtr *system = new SystemPtr(System::GetInstance());
        m_system = system;

        CameraList camList = (*system)->GetCameras();
        unsigned int numCameras = camList.GetSize();

        if (numCameras == 0) {
            camList.Clear();
            emit errorOccurred(tr("No Spinnaker cameras found."));
            return false;
        }

        CameraPtr *camera = nullptr;
        if (deviceId.isEmpty()) {
            camera = new CameraPtr(camList.GetByIndex(0));
        } else {
            for (unsigned int i = 0; i < numCameras; i++) {
                CameraPtr cam = camList.GetByIndex(i);
                INodeMap& nodeMapTLDevice = cam->GetTLDeviceNodeMap();
                CStringPtr ptrDeviceID = nodeMapTLDevice.GetNode("DeviceID");
                if (IsAvailable(ptrDeviceID) && IsReadable(ptrDeviceID)) {
                    QString id = QString::fromStdString(ptrDeviceID->GetValue().c_str());
                    if (id == deviceId) {
                        camera = new CameraPtr(cam);
                        break;
                    }
                }
            }
        }

        camList.Clear();

        if (!camera) {
            emit errorOccurred(tr("Failed to find requested Spinnaker camera."));
            return false;
        }

        m_camera = camera;
        (*camera)->Init();

        INodeMap& nodeMap = (*camera)->GetNodeMap();

        CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
        if (IsAvailable(ptrAcquisitionMode) && IsWritable(ptrAcquisitionMode)) {
            CEnumEntryPtr ptrContinuous = ptrAcquisitionMode->GetEntryByName("Continuous");
            if (IsAvailable(ptrContinuous) && IsReadable(ptrContinuous))
                ptrAcquisitionMode->SetIntValue(ptrContinuous->GetValue());
        }

        // Prefer raw Bayer formats: lower bus bandwidth (1 byte/pixel vs 3),
        // camera free from ISP work, and host GPU handles demosaic.
        // Fall back to RGB8Packed or Mono8 if no Bayer format is available.
        struct FmtEntry { const char *name; int pattern; bool isBayer; };
        static const FmtEntry fmtPriority[] = {
            {"BayerRG8", 0, true},
            {"BayerBG8", 1, true},
            {"BayerGR8", 2, true},
            {"BayerGB8", 3, true},
            {"BGR8",     0, false},
            {"RGB8Packed", 0, false},
            {"Mono8",    0, false},
        };

        m_emitRaw = false;
        CEnumerationPtr ptrPixelFormat = nodeMap.GetNode("PixelFormat");
        if (IsAvailable(ptrPixelFormat) && IsWritable(ptrPixelFormat)) {
            for (const auto &fmt : fmtPriority) {
                CEnumEntryPtr ptrEntry = ptrPixelFormat->GetEntryByName(fmt.name);
                if (IsAvailable(ptrEntry) && IsReadable(ptrEntry)) {
                    ptrPixelFormat->SetIntValue(ptrEntry->GetValue());
                    m_bayerPattern = fmt.pattern;
                    m_emitRaw      = fmt.isBayer;
                    ppDebug() << "[VideoInputSpinnaker] Pixel format:" << fmt.name
                             << (fmt.isBayer ? "(raw Bayer, GPU demosaic)" : "(pre-decoded)");
                    break;
                }
            }
        }

        // Increase the image buffer pool. StreamBufferCount lives in the
        // TL Stream node map (transport layer), NOT the device node map.
        // Default pool size is typically 10; raw Bayer (1 byte/pixel) fills
        // buffers 3x faster than BGR8, exhausting the pool at high frame rates.
        INodeMap &streamMap = (*camera)->GetTLStreamNodeMap();
        CEnumerationPtr ptrBufferMode = streamMap.GetNode("StreamBufferCountMode");
        CIntegerPtr ptrBufferCount = streamMap.GetNode("StreamBufferCountManual");
        if (IsAvailable(ptrBufferMode) && IsWritable(ptrBufferMode)) {
            CEnumEntryPtr ptrManual = ptrBufferMode->GetEntryByName("Manual");
            if (IsAvailable(ptrManual) && IsReadable(ptrManual))
                ptrBufferMode->SetIntValue(ptrManual->GetValue());
        }
        if (IsAvailable(ptrBufferCount) && IsWritable(ptrBufferCount)) {
            const int64_t desired = 40;
            ptrBufferCount->SetValue(
                qBound(ptrBufferCount->GetMin(), desired, ptrBufferCount->GetMax()));
            ppDebug() << "[VideoInputSpinnaker] Stream buffer count:"
                     << ptrBufferCount->GetValue();
        }

        (*camera)->BeginAcquisition();
        m_streaming = true;
        m_state = State::Active;
        emit stateChanged(State::Active);

        (void)QtConcurrent::run([this]() { captureLoop(); });

        return true;
    } catch (Spinnaker::Exception &e) {
        emit errorOccurred(tr("Spinnaker error: %1").arg(e.what()));
        return false;
    }
#else
    Q_UNUSED(deviceId)
    return false;
#endif
}

void VideoInputSpinnaker::stop()
{
    m_abort = true;

#ifdef HAVE_SPINNAKER
    if (m_camera) {
        CameraPtr *camera = (CameraPtr*)m_camera;
        try {
            if (m_streaming) {
                (*camera)->EndAcquisition();
                m_streaming = false;
            }
            (*camera)->DeInit();
        } catch (...) {}
        delete camera;
        m_camera = nullptr;
    }

    if (m_system) {
        SystemPtr *system = (SystemPtr*)m_system;
        try {
            (*system)->ReleaseInstance();
        } catch (...) {}
        delete system;
        m_system = nullptr;
    }
#endif

    if (m_state != State::Stopped) {
        m_state = State::Stopped;
        emit stateChanged(State::Stopped);
    }
}

void VideoInputSpinnaker::suspend()
{
    if (m_state == State::Active) {
        m_state = State::Suspended;
        emit stateChanged(State::Suspended);
    }
}

void VideoInputSpinnaker::resume()
{
    if (m_state == State::Suspended) {
        m_state = State::Active;
        emit stateChanged(State::Active);
    }
}

bool VideoInputSpinnaker::isActive() const
{
    return m_state == State::Active;
}

QVideoFrameFormat VideoInputSpinnaker::frameFormat() const
{
    return QVideoFrameFormat();
}

void VideoInputSpinnaker::captureLoop()
{
#ifdef HAVE_SPINNAKER
    while (!m_abort && m_camera) {
        CameraPtr *camera = (CameraPtr*)m_camera;
        try {
            ImagePtr pResultImage = (*camera)->GetNextImage(1000);
            if (pResultImage->IsIncomplete()) {
                ppDebug() << "[VideoInputSpinnaker] Incomplete image, status"
                         << pResultImage->GetImageStatus();
                pResultImage->Release();
                continue;
            }

            const size_t width  = pResultImage->GetWidth();
            const size_t height = pResultImage->GetHeight();
            const size_t stride = pResultImage->GetStride();
            const uchar *src    = static_cast<const uchar*>(pResultImage->GetData());

            PixelFormatEnums fmt = pResultImage->GetPixelFormat();
            bool isBayer = (fmt == PixelFormat_BayerRG8 || fmt == PixelFormat_BayerBG8 ||
                            fmt == PixelFormat_BayerGR8 || fmt == PixelFormat_BayerGB8);
            bool isRGB   = (fmt == PixelFormat_RGB8Packed);
            bool isBGR   = (fmt == PixelFormat_BGR8);

            if (isBayer && m_emitRaw) {
                // Hot path: copy raw Bayer bytes once; GPU demosaics on the display thread.
                // Pack rows (remove any stride padding) so the GPU upload path is simple.
                RawVideoFrame rawFrame;
                rawFrame.width   = static_cast<int>(width);
                rawFrame.height  = static_cast<int>(height);
                rawFrame.pattern = static_cast<RawVideoFrame::BayerPattern>(m_bayerPattern);
                rawFrame.data.resize(static_cast<qsizetype>(width * height));
                char *dst = rawFrame.data.data();
                if (stride == width) {
                    memcpy(dst, src, width * height);
                } else {
                    for (size_t y = 0; y < height; ++y)
                        memcpy(dst + y * width, src + y * stride, width);
                }
                pResultImage->Release();

                QMetaObject::invokeMethod(this, [this, rawFrame = std::move(rawFrame)]() mutable {
                    emit rawVideoFrameReady(rawFrame);
                }, Qt::QueuedConnection);

            } else {
                // Pre-decoded path (RGB8Packed, BGR8, Mono8): wrap and emit as QVideoFrame.
                QImage img;
                if (isRGB) {
                    img = QImage(src, static_cast<int>(width), static_cast<int>(height),
                                 static_cast<int>(width) * 3, QImage::Format_RGB888).copy();
                } else if (isBGR) {
                    img = QImage(src, static_cast<int>(width), static_cast<int>(height),
                                 static_cast<int>(width) * 3, QImage::Format_BGR888).copy();
                } else {
                    img = QImage(src, static_cast<int>(width), static_cast<int>(height),
                                 QImage::Format_Grayscale8).copy();
                }
                pResultImage->Release();

                QVideoFrame frame(img);
                QMetaObject::invokeMethod(this, [this, frame]() {
                    emit videoFrameReady(frame);
                }, Qt::QueuedConnection);
            }

        } catch (Spinnaker::Exception &e) {
            // -1012 (SPINNAKER_ERR_ABORT) is the normal consequence of
            // EndAcquisition() being called while GetNextImage() is waiting.
            // Only log it when the abort was unexpected (m_abort is still false).
            if (!m_abort)
                ppWarn() << "[VideoInputSpinnaker] Capture error:" << e.what();
        }
    }
#endif
}
