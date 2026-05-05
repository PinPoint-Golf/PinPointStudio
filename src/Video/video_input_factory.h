#pragma once

#include <QObject>
#include <memory>

class VideoInputBase;

class VideoInputFactory : public QObject
{
    Q_OBJECT

public:
    enum class Backend {
        Auto,
        QtMultimedia,
        AppleAVFoundation,
        Aravis
    };
    Q_ENUM(Backend)

    // Creates the most appropriate backend for the current platform and hardware.
    // If backend is Auto, it will:
    // 1. Try Aravis if HAVE_ARAVIS is defined and a camera is found.
    // 2. Try AppleAVFoundation on macOS.
    // 3. Fall back to QtMultimedia.
    static VideoInputBase* create(Backend backend = Backend::Auto, QObject *parent = nullptr);

    // Returns the backend type of the given input.
    static Backend backendType(VideoInputBase *input);

    // Returns a list of available backends on this system.
    static QList<Backend> availableBackends();
};
