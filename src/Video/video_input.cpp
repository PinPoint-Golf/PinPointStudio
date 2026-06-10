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

#include "video_input.h"
#include "../Core/device_enumerator.h"

#include <QCamera>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoSink>
#include <QDateTime>
#include <QFileInfo>

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <cfgmgr32.h>

// Walk the Windows device tree from a Media Foundation camera symlink up to the
// USB device node and return the serial number embedded in its instance ID.
// The symlink from QCameraDevice::id() looks like:
//   \\?\usb#vid_046d&pid_0825&mi_00#<serial_or_port_hash>#{e5323777-...}
// Converting '#' → '\' and stripping the \\?\ prefix and #{guid} suffix gives
// the device instance ID, which CM_Locate_DevNodeW can resolve directly.
static QString usbSerialFromCameraPath(const QByteArray &id)
{
    QString path = QString::fromUtf8(id);
    if (path.startsWith("\\\\?\\")) path = path.mid(4);
    int brace = path.indexOf('{');
    if (brace > 0) path = path.left(brace - 1); // strip trailing #
    path = path.replace('#', '\\').toUpper();
    // path is now e.g. "USB\VID_046D&PID_0825&MI_00\6&2A3B4C5D&0&0000"

    DEVINST devInst = 0;
    if (CM_Locate_DevNodeW(&devInst,
            reinterpret_cast<DEVINSTID_W>(path.toStdWString().data()),
            CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return {};

    // Walk up the parent chain to the USB device node (max 3 levels covers
    // interface → composite → USB device).
    for (int depth = 0; depth < 3; ++depth) {
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, devInst, 0) != CR_SUCCESS) break;

        wchar_t instanceId[512] = {};
        if (CM_Get_Device_IDW(parent, instanceId, 512, 0) != CR_SUCCESS) break;

        QString parentId = QString::fromWCharArray(instanceId);
        if (parentId.startsWith("USB\\", Qt::CaseInsensitive)) {
            QString serial = parentId.section('\\', -1);
            // Windows uses "6&XXXXXXXX&0&PORT" when the device has no serial.
            if (!serial.isEmpty() && !serial.startsWith("6&", Qt::CaseInsensitive))
                return serial;
            return {};
        }
        devInst = parent;
    }
    return {};
}
#endif // Q_OS_WIN

VideoInput::VideoInput(QObject *parent)
    : VideoInputBase(parent)
{
}

VideoInput::~VideoInput()
{
    stop();
}

QList<QCameraDevice> VideoInput::availableDevices()
{
    const QList<QCameraDevice> devs = QMediaDevices::videoInputs();
    for (const auto &dev : devs) {
        DeviceEnumerator::instance()->registerDevice(
            DeviceType::VideoInput, VideoInputFactory::Backend::QtMultimedia,
            dev.id(), dev.description(),
            VideoInput::capabilitiesFor(dev));
    }
    return devs;
}

// File-scope helper shared by capabilitiesFor() and queryCapabilities().
static PixelFormat qtFormatToPixelFormat(QVideoFrameFormat::PixelFormat qfmt)
{
    PixelFormat pf;
    pf.nativeKey = QVideoFrameFormat::pixelFormatToString(qfmt);
    switch (qfmt) {
    case QVideoFrameFormat::Format_YUYV:
        pf.encoding = PixelEncoding::YUV422_YUYV; pf.bitsPerPixel = 16; break;
    case QVideoFrameFormat::Format_UYVY:
        pf.encoding = PixelEncoding::YUV422_UYVY; pf.bitsPerPixel = 16; break;
    case QVideoFrameFormat::Format_NV12:
        pf.encoding = PixelEncoding::YUV420_NV12; pf.bitsPerPixel = 12; break;
    case QVideoFrameFormat::Format_YUV420P:
        pf.encoding = PixelEncoding::YUV420_I420; pf.bitsPerPixel = 12; break;
    case QVideoFrameFormat::Format_Jpeg:
        pf.encoding = PixelEncoding::MJPEG;        pf.bitsPerPixel = 0;  break;
    case QVideoFrameFormat::Format_BGRA8888:
    case QVideoFrameFormat::Format_BGRX8888:
        pf.encoding = PixelEncoding::BGR8;         pf.bitsPerPixel = 32; break;
    case QVideoFrameFormat::Format_RGBA8888:
    case QVideoFrameFormat::Format_RGBX8888:
        pf.encoding = PixelEncoding::RGB8;         pf.bitsPerPixel = 32; break;
    default:
        pf.encoding = PixelEncoding::Unknown;      pf.bitsPerPixel = 0;  break;
    }
    return pf;
}

CameraCapabilities VideoInput::capabilitiesFor(const QCameraDevice &dev)
{
    CameraCapabilities caps;
    if (dev.isNull()) return caps;

    caps.driverVersion        = "Qt6 Multimedia";
    caps.connectionInterface  = CameraCapabilities::Interface::USB3;
    caps.modelName            = dev.description();

#if defined(HAVE_LIBUDEV)
    // Walk the udev device tree to find the USB serial number for this V4L2 node.
    // QCameraDevice::id() is the raw device node path (e.g. "/dev/video0") on Linux,
    // which is port-order dependent and can change across reboots with multiple cameras.
    {
        QString devNode = QString::fromUtf8(dev.id());
        QString sysName = QFileInfo(devNode).fileName(); // "video0"
        struct udev *udev = udev_new();
        if (udev) {
            struct udev_device *vdev = udev_device_new_from_subsystem_sysname(
                udev, "video4linux", sysName.toLocal8Bit().constData());
            if (vdev) {
                struct udev_device *usb = udev_device_get_parent_with_subsystem_devtype(
                    vdev, "usb", "usb_device");
                if (usb) {
                    const char *sn = udev_device_get_sysattr_value(usb, "serial");
                    if (sn && sn[0]) caps.serialNumber = QString::fromLocal8Bit(sn);
                    const char *mfr = udev_device_get_sysattr_value(usb, "manufacturer");
                    if (mfr && mfr[0]) caps.vendorName  = QString::fromLocal8Bit(mfr);
                    const char *prod = udev_device_get_sysattr_value(usb, "product");
                    if (prod && prod[0]) caps.modelName  = QString::fromLocal8Bit(prod);
                }
                udev_device_unref(vdev);
            }
            udev_unref(udev);
        }
    }
#elif defined(Q_OS_WIN)
    // Walk the Windows device tree from the Media Foundation symlink up to the
    // USB device node to read the manufacturer-programmed serial number.
    // Falls back to empty if the device has no serial (port-hash is discarded).
    {
        QString serial = usbSerialFromCameraPath(dev.id());
        if (!serial.isEmpty()) caps.serialNumber = serial;
    }
#else
    // macOS: QCameraDevice::id() is AVCaptureDevice.uniqueID — hardware-derived.
    caps.serialNumber = QString::fromUtf8(dev.id());
#endif

    caps.pixelFormat.kind     = CapabilityKind::Discrete;
    caps.pixelFormat.writable = true;
    for (const QCameraFormat &fmt : dev.videoFormats()) {
        PixelFormat pf = qtFormatToPixelFormat(fmt.pixelFormat());
        bool found = false;
        for (const auto &existing : caps.pixelFormat.supported)
            if (existing.nativeKey == pf.nativeKey) { found = true; break; }
        if (!found) caps.pixelFormat.supported.append(pf);
    }

    caps.resolution.kind     = CapabilityKind::Discrete;
    caps.resolution.writable = true;
    for (const QCameraFormat &fmt : dev.videoFormats()) {
        Resolution r{ fmt.resolution().width(), fmt.resolution().height() };
        bool found = false;
        for (const auto &existing : caps.resolution.presets)
            if (existing.width == r.width && existing.height == r.height) { found = true; break; }
        if (!found) caps.resolution.presets.append(r);
    }

    double minFps = 1e9, maxFps = 0.0;
    for (const QCameraFormat &fmt : dev.videoFormats()) {
        minFps = qMin(minFps, (double)fmt.minFrameRate());
        maxFps = qMax(maxFps, (double)fmt.maxFrameRate());
    }
    if (maxFps > 0.0) {
        caps.frameRate.kind               = CapabilityKind::Range;
        caps.frameRate.range.min          = minFps;
        caps.frameRate.range.max          = maxFps;
        caps.frameRate.range.step         = 0;
        caps.frameRate.range.defaultValue = maxFps;
        caps.frameRate.readable           = true;
        caps.frameRate.writable           = true;
    }

    // Qt orders videoFormats() from most-preferred to least; first entry is
    // what the OS/driver will select if no format is explicitly configured.
    if (!dev.videoFormats().isEmpty()) {
        const QCameraFormat preferred = dev.videoFormats().first();
        caps.pixelFormat.defaultFormat    = qtFormatToPixelFormat(preferred.pixelFormat());
        caps.resolution.defaultResolution = {
            preferred.resolution().width(), preferred.resolution().height()
        };
        if (caps.frameRate.kind == CapabilityKind::Range)
            caps.frameRate.range.defaultValue = preferred.maxFrameRate();
    }

    return caps;
}

void VideoInput::prepareDevice(const QString &deviceId)
{
    m_targetDeviceId = deviceId;
}

bool VideoInput::start(const QString &deviceId)
{
    stop();

    const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    if (devices.isEmpty()) {
        emit errorOccurred(tr("No camera devices available"));
        return false;
    }

    QCameraDevice activeDevice = QMediaDevices::defaultVideoInput();
    if (!deviceId.isEmpty()) {
        // A specific device was requested — it must actually be found.
        // Silently falling back to the default camera records from the WRONG
        // physical device (with the requested device's alias/perspective/crop
        // applied) whenever the id is stale, e.g. unplug/replug moved the
        // V4L2 node.
        bool found = false;
        for (const QCameraDevice &dev : devices) {
            if (dev.description() == deviceId || dev.id() == deviceId.toUtf8()) {
                activeDevice = dev;
                found = true;
                break;
            }
        }
        if (!found) {
            emit errorOccurred(tr("Camera \"%1\" not found — it may have been "
                                  "unplugged or re-enumerated").arg(deviceId));
            return false;
        }
    }

    m_session = new QMediaCaptureSession(this);
    m_camera  = new QCamera(activeDevice, this);
    m_sink    = new QVideoSink(this);

    // Pin the format to the first (highest-quality) entry so that the
    // capabilities enumerated from dev.videoFormats().first() match what the
    // camera actually delivers.  Without this, Qt auto-selects opaquely and
    // cameraFormat() may return null, making capabilities unreliable.
    if (!activeDevice.videoFormats().isEmpty())
        m_camera->setCameraFormat(activeDevice.videoFormats().first());

    m_session->setCamera(m_camera);
    m_session->setVideoSink(m_sink);

    connect(m_camera, &QCamera::activeChanged,
            this, &VideoInput::onCameraActiveChanged);
    connect(m_camera, &QCamera::errorOccurred,
            this, &VideoInput::onCameraErrorOccurred);
    // QVideoSink may emit from any thread; queue to stay on our thread.
    connect(m_sink, &QVideoSink::videoFrameChanged,
            this, &VideoInput::onVideoFrameChanged,
            Qt::QueuedConnection);

    m_camera->start();

    return true;
}

void VideoInput::stop()
{
    if (m_camera) {
        disconnect(m_camera, nullptr, this, nullptr);
        m_camera->stop();
    }
    if (m_sink)
        disconnect(m_sink, nullptr, this, nullptr);

    if (m_session) {
        m_session->setCamera(nullptr);
        m_session->setVideoSink(nullptr);
        delete m_session;
        m_session = nullptr;
    }
    delete m_camera;  m_camera = nullptr;
    delete m_sink;    m_sink   = nullptr;

    if (m_state != State::Stopped) {
        m_state = State::Stopped;
        emit stateChanged(State::Stopped);
    }
}

void VideoInput::suspend()
{
    if (m_camera && m_state == State::Active) {
        m_state = State::Suspended;
        m_camera->stop();
        emit stateChanged(State::Suspended);
    }
}

void VideoInput::resume()
{
    if (m_camera && m_state == State::Suspended)
        m_camera->start();
}

bool VideoInput::isActive() const
{
    return m_state == State::Active;
}

QVideoFrameFormat VideoInput::frameFormat() const
{
    if (m_sink) {
        const QVideoFrameFormat fmt = m_sink->videoFrame().surfaceFormat();
        if (fmt.isValid())
            return fmt;
    }
    return m_preferredFormat;
}

void VideoInput::setPreferredFormat(const QVideoFrameFormat &format)
{
    m_preferredFormat = format;
}

void VideoInput::onCameraActiveChanged(bool active)
{
    if (active) {
        if (m_state != State::Active) {
            m_state = State::Active;
            emit stateChanged(State::Active);
        }
    } else {
        // Don't clobber Suspended or Error — only transition from Active.
        if (m_state == State::Active) {
            m_state = State::Stopped;
            emit stateChanged(State::Stopped);
        }
    }
}

void VideoInput::onCameraErrorOccurred(QCamera::Error error, const QString &errorString)
{
    if (error != QCamera::NoError) {
        m_state = State::Error;
        emit errorOccurred(errorString);
        emit stateChanged(State::Error);
    }
}

void VideoInput::onVideoFrameChanged(const QVideoFrame &frame)
{
    if (frame.isValid())
        emit videoFrameReady(frame);
}

CameraCapabilities VideoInput::queryCapabilities() const
{
    QCameraDevice dev;
    if (m_camera)
        dev = m_camera->cameraDevice();
    if (dev.isNull() && !m_targetDeviceId.isEmpty()) {
        for (const QCameraDevice &d : QMediaDevices::videoInputs()) {
            if (d.description() == m_targetDeviceId
                    || d.id() == m_targetDeviceId.toUtf8()) {
                dev = d;
                break;
            }
        }
    }
    if (dev.isNull())
        dev = QMediaDevices::defaultVideoInput();

    // Start with the static (no-connection) capabilities.
    CameraCapabilities caps = VideoInput::capabilitiesFor(dev);
    caps.queriedAt = QDateTime::currentDateTime();

    // Post-start: use the actual delivered frame from the sink as ground truth.
    // m_sink->videoFrame() is valid once the camera has delivered at least one
    // frame — this is the only reliable source of the true negotiated format on
    // platforms where QCamera::cameraFormat() returns null for auto-selected modes.
    if (m_camera && m_camera->isActive()) {
        const QVideoFrameFormat sinkFmt =
            m_sink ? m_sink->videoFrame().surfaceFormat() : QVideoFrameFormat{};

        if (sinkFmt.isValid()) {
            // Ground truth: use what the camera is actually delivering.
            caps.pixelFormat.defaultFormat    = qtFormatToPixelFormat(sinkFmt.pixelFormat());
            caps.resolution.defaultResolution = {
                sinkFmt.frameWidth(), sinkFmt.frameHeight()
            };
            // fps from cameraFormat() if available, otherwise leave the pre-start estimate.
            const QCameraFormat camFmt = m_camera->cameraFormat();
            if (!camFmt.isNull() && caps.frameRate.kind == CapabilityKind::Range)
                caps.frameRate.range.defaultValue = camFmt.maxFrameRate();
        } else {
            // No frame yet (called immediately at stateChanged(Active) before
            // first frame arrives) — fall back to cameraFormat().
            const QCameraFormat camFmt = m_camera->cameraFormat();
            if (!camFmt.isNull()) {
                caps.pixelFormat.defaultFormat    = qtFormatToPixelFormat(camFmt.pixelFormat());
                caps.resolution.defaultResolution = {
                    camFmt.resolution().width(), camFmt.resolution().height()
                };
                if (caps.frameRate.kind == CapabilityKind::Range)
                    caps.frameRate.range.defaultValue = camFmt.maxFrameRate();
            }
        }
    }

    return caps;
}
