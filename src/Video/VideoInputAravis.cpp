// Include Aravis headers before anything else and handle the 'signals' keyword conflict
#ifdef HAVE_ARAVIS
#undef signals
#include <arv.h>
#define signals public
#endif

#include "VideoInputAravis.h"
#include <QVideoFrame>
#include "pp_debug.h"
#include <QtConcurrent>

VideoInputAravis::VideoInputAravis(QObject *parent)
    : VideoInputBase(parent)
{
}

VideoInputAravis::~VideoInputAravis()
{
    stop();
}

bool VideoInputAravis::start(const QString &deviceId)
{
    stop();
    m_abort = false;

#ifdef HAVE_ARAVIS
    arv_update_device_list();
    if (arv_get_n_devices() == 0) {
        emit errorOccurred(tr("No Aravis devices found."));
        return false;
    }

    // Open device
    const char *id = deviceId.isEmpty() ? nullptr : deviceId.toLocal8Bit().constData();
    m_camera = arv_camera_new(id, nullptr);
    if (!m_camera) {
        emit errorOccurred(tr("Failed to open Aravis camera."));
        return false;
    }

    // Configure camera (typical high-speed defaults)
    ArvCamera *cam = (ArvCamera*)m_camera;
    arv_camera_set_region(cam, 0, 0, 1280, 720, nullptr); // Default 720p
    arv_camera_set_frame_rate(cam, 60.0, nullptr);        // Target 60 FPS
    arv_camera_set_pixel_format(cam, ARV_PIXEL_FORMAT_MONO_8, nullptr); // Raw Bayer or Mono

    // Create stream
    m_stream = arv_camera_create_stream(cam, nullptr, nullptr, nullptr);
    if (!m_stream) {
        emit errorOccurred(tr("Failed to create Aravis stream."));
        g_object_unref(m_camera);
        m_camera = nullptr;
        return false;
    }

    // Add buffers to stream
    ArvStream *stream = (ArvStream*)m_stream;
    for (int i = 0; i < 10; i++) {
        arv_stream_push_buffer(stream, arv_buffer_new(arv_camera_get_payload(cam, nullptr), nullptr));
    }

    arv_camera_start_acquisition(cam, nullptr);
    m_streaming = true;
    m_state = State::Active;
    emit stateChanged(State::Active);

    // Start capture loop in a background thread
    (void)QtConcurrent::run([this]() { captureLoop(); });

    return true;
#else
    Q_UNUSED(deviceId)
    return false;
#endif
}

void VideoInputAravis::stop()
{
    m_abort = true;

#ifdef HAVE_ARAVIS
    if (m_camera && m_streaming) {
        arv_camera_stop_acquisition((ArvCamera*)m_camera, nullptr);
        m_streaming = false;
    }

    if (m_stream) {
        g_object_unref(m_stream);
        m_stream = nullptr;
    }

    if (m_camera) {
        g_object_unref(m_camera);
        m_camera = nullptr;
    }
#endif

    if (m_state != State::Stopped) {
        m_state = State::Stopped;
        emit stateChanged(State::Stopped);
    }
}

void VideoInputAravis::suspend()
{
    if (m_state == State::Active) {
        m_state = State::Suspended;
        emit stateChanged(State::Suspended);
    }
}

void VideoInputAravis::resume()
{
    if (m_state == State::Suspended) {
        m_state = State::Active;
        emit stateChanged(State::Active);
    }
}

bool VideoInputAravis::isActive() const
{
    return m_state == State::Active;
}

QVideoFrameFormat VideoInputAravis::frameFormat() const
{
    return QVideoFrameFormat();
}

void VideoInputAravis::captureLoop()
{
#ifdef HAVE_ARAVIS
    while (!m_abort && m_stream) {
        ArvStream *stream = (ArvStream*)m_stream;
        ArvBuffer *buffer = arv_stream_pop_buffer(stream);
        if (buffer) {
            if (arv_buffer_get_status(buffer) == ARV_BUFFER_STATUS_SUCCESS) {
                size_t size;
                const void *data = arv_buffer_get_data(buffer, &size);
                int width, height;
                arv_buffer_get_image_region(buffer, nullptr, nullptr, &width, &height);

                // Create a QVideoFrame from the buffer.
                // We use Format_Grayscale8 for raw Bayer data.
                QImage img((const uchar*)data, width, height, QImage::Format_Grayscale8);
                QVideoFrame frame(img.copy()); // Copy data to ensure it stays valid

                QMetaObject::invokeMethod(this, [this, frame]() {
                    emit videoFrameReady(frame);
                }, Qt::QueuedConnection);
            }
            arv_stream_push_buffer(stream, buffer);
        }
    }
#endif
}
