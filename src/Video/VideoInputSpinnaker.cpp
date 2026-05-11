#ifdef HAVE_SPINNAKER
// Include Spinnaker headers before Qt to avoid macro conflicts (like 'signals')
#include "SpinnakerPlatform.h"
#undef SPINNAKER_DEPRECATED_CLASS
#define SPINNAKER_DEPRECATED_CLASS(msg) class SPINNAKER_API __declspec(deprecated(msg))
#include "Spinnaker.h"
#include "SpinGenApi/SpinnakerGenApi.h"
#endif

#ifdef HAVE_OPENCV
#include <opencv2/imgproc.hpp>
#endif

#include "VideoInputSpinnaker.h"
#include <QDebug>
#include <QVideoFrame>
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

        // Configure camera (similar to Aravis)
        INodeMap& nodeMap = (*camera)->GetNodeMap();
        
        // Set Acquisition Mode to Continuous
        CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
        if (IsAvailable(ptrAcquisitionMode) && IsWritable(ptrAcquisitionMode)) {
            CEnumEntryPtr ptrAcquisitionModeContinuous = ptrAcquisitionMode->GetEntryByName("Continuous");
            if (IsAvailable(ptrAcquisitionModeContinuous) && IsReadable(ptrAcquisitionModeContinuous)) {
                ptrAcquisitionMode->SetIntValue(ptrAcquisitionModeContinuous->GetValue());
            }
        }

        // Prefer RGB8Packed (no conversion), then Bayer (SDK conversion), then Mono8.
        CEnumerationPtr ptrPixelFormat = nodeMap.GetNode("PixelFormat");
        if (IsAvailable(ptrPixelFormat) && IsWritable(ptrPixelFormat)) {
            const char* formats[] = {"RGB8Packed", "BayerRG8", "BayerBG8", "BayerGR8", "BayerGB8", "Mono8"};
            for (const char* fmtName : formats) {
                CEnumEntryPtr ptrEntry = ptrPixelFormat->GetEntryByName(fmtName);
                if (IsAvailable(ptrEntry) && IsReadable(ptrEntry)) {
                    ptrPixelFormat->SetIntValue(ptrEntry->GetValue());
                    qDebug() << "[VideoInputSpinnaker] Pixel format set to" << fmtName;
                    break;
                }
            }
        }

        (*camera)->BeginAcquisition();
        m_streaming = true;
        m_state = State::Active;
        emit stateChanged(State::Active);

        // Start capture loop in a background thread
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
#ifndef HAVE_OPENCV
    // ImageProcessor only needed when OpenCV is unavailable for Bayer conversion.
    ImageProcessor processor;
    processor.SetColorProcessing(SPINNAKER_COLOR_PROCESSING_ALGORITHM_NEAREST_NEIGHBOR);
#endif

    while (!m_abort && m_camera) {
        CameraPtr *camera = (CameraPtr*)m_camera;
        try {
            ImagePtr pResultImage = (*camera)->GetNextImage(1000); // 1s timeout
            if (pResultImage->IsIncomplete()) {
                qDebug() << "[VideoInputSpinnaker] Image incomplete with status" << pResultImage->GetImageStatus();
            } else {
                size_t width  = pResultImage->GetWidth();
                size_t height = pResultImage->GetHeight();

                PixelFormatEnums fmt = pResultImage->GetPixelFormat();
                bool isBayer = (fmt == PixelFormat_BayerRG8 || fmt == PixelFormat_BayerBG8 ||
                                fmt == PixelFormat_BayerGR8 || fmt == PixelFormat_BayerGB8);
                bool isRGB   = (fmt == PixelFormat_RGB8Packed);
                bool isBGR   = (fmt == PixelFormat_BGR8);

                QImage img;
                if (isRGB) {
                    img = QImage(static_cast<const uchar*>(pResultImage->GetData()),
                                 static_cast<int>(width), static_cast<int>(height),
                                 static_cast<int>(width) * 3,
                                 QImage::Format_RGB888).copy();
                } else if (isBGR) {
                    // Keep as BGR888; drainDisplayFrame converts to RGB888 at display rate.
                    img = QImage(static_cast<const uchar*>(pResultImage->GetData()),
                                 static_cast<int>(width), static_cast<int>(height),
                                 static_cast<int>(width) * 3,
                                 QImage::Format_BGR888).copy();
                } else if (isBayer) {
#ifdef HAVE_OPENCV
                    // OpenCV's SIMD-optimised Bayer→BGR demosaic.
                    // Map Spinnaker pixel format to the matching OpenCV conversion code.
                    int cvtCode;
                    if      (fmt == PixelFormat_BayerRG8) cvtCode = cv::COLOR_BayerRGGB2BGR;
                    else if (fmt == PixelFormat_BayerBG8) cvtCode = cv::COLOR_BayerBGGR2BGR;
                    else if (fmt == PixelFormat_BayerGR8) cvtCode = cv::COLOR_BayerGRBG2BGR;
                    else                                   cvtCode = cv::COLOR_BayerGBRG2BGR;

                    // Wrap raw Bayer8 buffer without copying (read-only, valid until Release).
                    cv::Mat bayer(static_cast<int>(height), static_cast<int>(width), CV_8UC1,
                                  pResultImage->GetData(),
                                  static_cast<size_t>(pResultImage->GetStride()));
                    cv::Mat bgrMat;
                    cv::cvtColor(bayer, bgrMat, cvtCode);
                    // .copy() moves the result into Qt-managed heap memory so the D3D
                    // video pipeline gets a standard allocation (avoids display lag from
                    // the OpenCV allocator being handed directly to the upload path).
                    img = QImage(bgrMat.data,
                                 static_cast<int>(width), static_cast<int>(height),
                                 static_cast<int>(bgrMat.step[0]),
                                 QImage::Format_BGR888).copy();
#else
                    // Fallback: Spinnaker SDK conversion when OpenCV is unavailable.
                    ImagePtr converted = processor.Convert(pResultImage, PixelFormat_BGR8);
                    img = QImage(static_cast<const uchar*>(converted->GetData()),
                                 static_cast<int>(width), static_cast<int>(height),
                                 static_cast<int>(width) * 3,
                                 QImage::Format_BGR888).copy();
#endif
                } else {
                    img = QImage(static_cast<const uchar*>(pResultImage->GetData()),
                                 static_cast<int>(width), static_cast<int>(height),
                                 QImage::Format_Grayscale8).copy();
                }

                QVideoFrame frame(img);
                QMetaObject::invokeMethod(this, [this, frame]() {
                    emit videoFrameReady(frame);
                }, Qt::QueuedConnection);
            }
            pResultImage->Release();
        } catch (Spinnaker::Exception &e) {
            qDebug() << "[VideoInputSpinnaker] Capture error:" << e.what();
        }
    }
#endif
}
