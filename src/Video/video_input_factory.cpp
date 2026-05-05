#include "video_input_factory.h"
#include "video_input.h"

#ifdef Q_OS_MACOS
#include "VideoInputApple.h"
#endif

#ifdef HAVE_ARAVIS
#include "VideoInputAravis.h"
// Include Aravis headers last to avoid 'signals' conflict
#undef signals
#include <arv.h>
#define signals public
#endif

#include <QDebug>

VideoInputBase* VideoInputFactory::create(Backend backend, QObject *parent)
{
    if (backend == Backend::Auto) {
#ifdef HAVE_ARAVIS
        // Quick check if any Aravis devices are present
        arv_update_device_list();
        if (arv_get_n_devices() > 0) {
            qDebug() << "[VideoInputFactory] Industrial camera(s) detected; selecting Aravis backend.";
            return new VideoInputAravis(parent);
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
        default:
            qWarning() << "[VideoInputFactory] Requested backend not available on this platform; falling back to Qt Multimedia.";
            return new VideoInput(parent);
    }
}

VideoInputFactory::Backend VideoInputFactory::backendType(VideoInputBase *input)
{
    if (!input) return Backend::QtMultimedia;
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
    return list;
}
