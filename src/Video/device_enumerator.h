#pragma once

#include <QString>
#include <QList>
#include <QObject>
#include "video_input_factory.h"

enum class DeviceType {
    VideoInput,
    AudioInput,
    AudioOutput
};

struct Device {
    DeviceType type;
    VideoInputFactory::Backend backend; // For non-video, we can use a generic "System" or "Qt" backend
    QString id;
    QString description;
};

class DeviceEnumerator : public QObject
{
    Q_OBJECT

public:
    static DeviceEnumerator* instance();

    void enumerate();
    QList<Device> devices() const;

    void registerDevice(DeviceType type, VideoInputFactory::Backend backend, const QString &id, const QString &description);

private:
    explicit DeviceEnumerator(QObject *parent = nullptr);
    
    QList<Device> m_devices;
    bool m_videoEnumerated = false;
    bool m_audioInputEnumerated = false;
    bool m_audioOutputEnumerated = false;
};
