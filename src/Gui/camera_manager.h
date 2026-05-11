#pragma once

#include <QObject>
#include <QVariantList>
#include <QList>

#include "device_enumerator.h"

class VideoController;

class CameraManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList cameraList  READ cameraList  NOTIFY cameraListChanged)
    Q_PROPERTY(QVariantList instances   READ instances   NOTIFY instancesChanged)
    Q_PROPERTY(bool isRecording         READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool anySelected         READ anySelected NOTIFY instancesChanged)

public:
    explicit CameraManager(QObject *parent = nullptr);
    ~CameraManager() override;

    QVariantList cameraList() const;
    QVariantList instances()  const;
    bool isRecording()        const;
    bool anySelected()        const;

    Q_INVOKABLE void setSelected(int index, bool selected);
    Q_INVOKABLE void startAll();
    Q_INVOKABLE void stopAll();

signals:
    void cameraListChanged();
    void instancesChanged();
    void isRecordingChanged();

private:
    struct CameraEntry {
        Device device;
        bool selected = false;
        VideoController *controller = nullptr;
    };

    QList<CameraEntry> m_cameras;
    bool m_recording = false;

    VideoController *createController(const Device &device);
};
