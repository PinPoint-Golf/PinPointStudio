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

#pragma once

#include <QObject>
#include <QVariantMap>
#include <QVector>
#include <memory>

class QVideoSink;

// Per-stream metadata for the Review camera tiles. A reviewed swing may have been
// recorded on a completely different camera rig (stream count, perspectives,
// aspects), so this is read from the SWING'S OWN swing.json — never the local
// camera list. The tiles enumerate these, degrading gracefully when data is
// missing (no face-on stream → no club overlay; unknown aspect → 16:9).
struct ReplayStreamInfo {
    int    index       = 0;        // stream index (== setVideoSink index)
    int    perspective = -1;       // swing.json setup.perspective (0..3); 2 = FaceOn
    double aspect      = 16.0 / 9.0;   // source width / height
    bool   hasAnalysis = false;    // face-on stream that carries pose/club detail
};

// Abstract replay source — the thing ShotReplayController drives. Same abstract-
// base + factory shape as the IMU/STT/TTS backends: the controller owns a
// std::unique_ptr<ReplaySource> and forwards QML calls to it, so the concrete
// source can be swapped without touching the QML-facing controller.
//
// Today the only implementation is DiskReplaySource (MP4(s) + swing.json). A
// future InWindowReplaySource would read a live SwingWindow for the Capture-mode
// transient — the seam is here; it is intentionally NOT implemented now (the
// Capture transient keeps using ShotProcessor).
//
// Domain: everything is WINDOW-RELATIVE µs (capture time, t0 subtracted) —
// startUs..endUs span the captured frames, positionUs is the playhead, and
// analysisDetail series/phases/pose2d are offset to the same 0-based domain.
class ReplaySource : public QObject
{
    Q_OBJECT
public:
    explicit ReplaySource(QObject *parent = nullptr) : QObject(parent) {}
    ~ReplaySource() override = default;

    // Load the swing at `swingDir` and begin playback at `speed` (capture-time
    // multiplier, 1.0 = real time). Returns false if there is no playable stream;
    // a false return on a bad path/doc leaves any current replay intact, whereas a
    // valid doc with no playable stream unloads (streamCount() then 0).
    //
    // `trimToSwing` restricts *playback* (not the scrub range) to the detected
    // swing span — playback starts at the Address phase and stops at the Finish
    // phase, so the pre-address fidget and post-finish tail are skipped. It is a
    // no-op when the swing carries no phase analysis (falls back to the full span).
    virtual bool load(const QString &swingDir, double speed, bool trimToSwing) = 0;
    virtual void unload() = 0;

    virtual int                       streamCount()    const = 0;
    virtual QVector<ReplayStreamInfo> streams()        const = 0;
    virtual QVariantMap               analysisDetail() const = 0;
    virtual qint64 startUs()    const = 0;
    virtual qint64 endUs()      const = 0;
    virtual qint64 impactUs()   const = 0;
    virtual qint64 positionUs() const = 0;
    virtual bool   playing()    const = 0;
    virtual double speed()      const = 0;

    // Bind a QML VideoOutput's sink to stream `index`. Persists across loads (the
    // VideoOutput outlives a single replay), so it may be called before load().
    virtual void setVideoSink(int index, QVideoSink *sink) = 0;
    virtual void togglePlay()                = 0;
    virtual void seekToFraction(double frac) = 0;   // 0..1 over the window
    virtual void seekToUs(qint64 us)         = 0;
    virtual void stepFrame(int delta)        = 0;   // ±N frames on the master stream
    virtual void setSpeed(double speed)      = 0;
    virtual void beginScrub()                = 0;
    virtual void endScrub()                  = 0;

signals:
    void positionChanged();
    void spanChanged();
    void playingChanged();
    void speedChanged();
    // A stream failed to decode (human-readable reason for the UI). Non-fatal.
    void failed(const QString &error);
    // The source became unusable (e.g. the master stream is invalid media) and
    // tore itself down — the controller should clear its active state.
    void aborted();
    // Playback reached the natural end of the window (master stream EndOfMedia) —
    // distinct from a user pause. The host uses this to auto-return to Capture
    // after a post-shot auto-replay; a user-initiated replay reaching its end
    // emits it too, but the host gates the action on its own intent flag.
    void playbackEnded();
};

// Factory — today only the disk-backed source. The owner holds the returned
// unique_ptr; the source has no QObject parent (its lifetime is the unique_ptr).
std::unique_ptr<ReplaySource> makeDiskReplaySource();
