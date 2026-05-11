#include "video_input_factory.h"
#include "video_input.h"
#include "../Core/device_enumerator.h"

#ifdef Q_OS_MACOS
#include "VideoInputApple.h"
#include <QCameraDevice>
#include <QMediaDevices>
#endif

#ifdef HAVE_SPINNAKER
#include "SpinnakerPlatform.h"
#undef SPINNAKER_DEPRECATED_CLASS
#define SPINNAKER_DEPRECATED_CLASS(msg) class SPINNAKER_API __declspec(deprecated(msg))
#include <Spinnaker.h>
#include "VideoInputSpinnaker.h"
#endif

#ifdef HAVE_ARAVIS
#include "VideoInputAravis.h"
// Include Aravis headers last to avoid 'signals' conflict
#undef signals
#include <arv.h>
#define signals public
#endif

#include <QDebug>

void VideoInputFactory::enumerateDevices()
{
#ifdef Q_OS_MACOS
    // On macOS the Qt Multimedia camera backend (VideoInput/QCamera) cannot
    // obtain permission because QCameraPermission is only compiled into the
    // AVFoundation multimedia plugin, not the FFmpeg one.  Enumerate cameras
    // via QMediaDevices but register them as AppleAVFoundation so that
    // CameraManager creates VideoInputApple instances (pure AVFoundation, no
    // QCameraPermission) for every device.
    for (const QCameraDevice &dev : QMediaDevices::videoInputs()) {
        DeviceEnumerator::instance()->registerDevice(
            DeviceType::VideoInput, Backend::AppleAVFoundation,
            dev.id(), dev.description());
    }
#else
    VideoInput::availableDevices();
#endif

#ifdef HAVE_ARAVIS
    arv_update_device_list();
    unsigned int nAravis = arv_get_n_devices();
    for (unsigned int i = 0; i < nAravis; ++i) {
        DeviceEnumerator::instance()->registerDevice(
            DeviceType::VideoInput, Backend::Aravis,
            QString::fromLocal8Bit(arv_get_device_id(i)),
            QString::fromLocal8Bit(arv_get_device_model(i))
        );
    }
#endif

#ifdef HAVE_SPINNAKER
    try {
        Spinnaker::SystemPtr system = Spinnaker::System::GetInstance();
        Spinnaker::CameraList camList = system->GetCameras();
        for (unsigned int i = 0; i < camList.GetSize(); ++i) {
            Spinnaker::CameraPtr cam = camList.GetByIndex(i);
            Spinnaker::GenApi::INodeMap& nodeMapTLDevice = cam->GetTLDeviceNodeMap();
            Spinnaker::GenApi::CStringPtr ptrDeviceID = nodeMapTLDevice.GetNode("DeviceID");
            Spinnaker::GenApi::CStringPtr ptrDeviceModel = nodeMapTLDevice.GetNode("DeviceModelName");

            QString id = "Unknown";
            QString model = "Spinnaker Camera";
            if (Spinnaker::GenApi::IsAvailable(ptrDeviceID) && Spinnaker::GenApi::IsReadable(ptrDeviceID))
                id = QString::fromStdString(ptrDeviceID->GetValue().c_str());
            if (Spinnaker::GenApi::IsAvailable(ptrDeviceModel) && Spinnaker::GenApi::IsReadable(ptrDeviceModel))
                model = QString::fromStdString(ptrDeviceModel->GetValue().c_str());

            DeviceEnumerator::instance()->registerDevice(
                DeviceType::VideoInput, Backend::Spinnaker, id, model);
        }
        camList.Clear();
        system->ReleaseInstance();
    } catch (...) {}
#endif
}

VideoInputBase* VideoInputFactory::create(Backend backend, QObject *parent)
{
    enumerateDevices();

    if (backend == Backend::Auto) {
#ifdef HAVE_SPINNAKER
        if (DeviceEnumerator::instance()->devices().count() > 0) {
            for (const auto &dev : DeviceEnumerator::instance()->devices()) {
                if (dev.backend == Backend::Spinnaker) {
                    qDebug() << "[VideoInputFactory] Spinnaker camera detected; selecting Spinnaker backend.";
                    return new VideoInputSpinnaker(parent);
                }
            }
        }
#endif

#ifdef HAVE_ARAVIS
        {
            bool hasAravis = false;
            for (const auto &dev : DeviceEnumerator::instance()->devices())
                if (dev.backend == Backend::Aravis) { hasAravis = true; break; }
            if (hasAravis) {
                qDebug() << "[VideoInputFactory] Industrial camera(s) detected; selecting Aravis backend.";
                return new VideoInputAravis(parent);
            }
        }
#endif

#ifdef Q_OS_MACOS
        qDebug() << "[VideoInputFactory] Selecting native Apple AVFoundation backend.";
        return new VideoInputApple(parent);
#endif

        qDebug() << "[VideoInputFactory] Selecting Qt Multimedia backend.";
        return new VideoInput(parent);
    }

    switch (backend) {
        case Backend::QtMultimedia:
            return new VideoInput(parent);
#ifdef Q_OS_MACOS
        case Backend::AppleAVFoundation:
            return new VideoInputApple(parent);
#endif
#ifdef HAVE_ARAVIS
        case Backend::Aravis:
            return new VideoInputAravis(parent);
#endif
#ifdef HAVE_SPINNAKER
        case Backend::Spinnaker:
            return new VideoInputSpinnaker(parent);
#endif
        default:
            qWarning() << "[VideoInputFactory] Requested backend not available on this platform; falling back to Qt Multimedia.";
            return new VideoInput(parent);
    }
}

VideoInputFactory::Backend VideoInputFactory::backendType(VideoInputBase *input)
{
    if (!input) return Backend::QtMultimedia;
#ifdef HAVE_SPINNAKER
    if (dynamic_cast<VideoInputSpinnaker*>(input)) return Backend::Spinnaker;
#endif
#ifdef HAVE_ARAVIS
    if (dynamic_cast<VideoInputAravis*>(input)) return Backend::Aravis;
#endif
#ifdef Q_OS_MACOS
    if (dynamic_cast<VideoInputApple*>(input)) return Backend::AppleAVFoundation;
#endif
    return Backend::QtMultimedia;
}

QList<VideoInputFactory::Backend> VideoInputFactory::availableBackends()
{
    QList<Backend> list;
    list << Backend::QtMultimedia;
#ifdef Q_OS_MACOS
    list << Backend::AppleAVFoundation;
#endif
#ifdef HAVE_ARAVIS
    list << Backend::Aravis;
#endif
#ifdef HAVE_SPINNAKER
    list << Backend::Spinnaker;
#endif
    return list;
}
