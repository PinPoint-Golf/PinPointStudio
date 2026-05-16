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

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include "VideoInputApple.h"

#include <QImage>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QDateTime>

// ---------------------------------------------------------------------------
// Private implementation struct (Obj-C strong pointers — ARC manages them)
// ---------------------------------------------------------------------------

struct VideoInputApplePrivate {
    AVCaptureSession             *session  = nil;
    AVCaptureVideoDataOutput     *output   = nil;
    // The delegate is declared below; forward-declare with id here.
    id                            delegate = nil;
};

// ---------------------------------------------------------------------------
// AVCaptureVideoDataOutputSampleBufferDelegate
// ---------------------------------------------------------------------------

@interface VideoInputAppleDelegate
    : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
// Atomic so the C++ side can nil it from stop() while the capture queue
// may still be mid-callback.
@property (atomic, assign) VideoInputApple *owner;
@end

@implementation VideoInputAppleDelegate

- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection
{
    Q_UNUSED(output)
    Q_UNUSED(connection)

    VideoInputApple *owner = self.owner;
    if (!owner)
        return;

    CVImageBufferRef buf = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!buf)
        return;

    CVPixelBufferLockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);

    const uchar *data = static_cast<const uchar *>(CVPixelBufferGetBaseAddress(buf));
    const int width   = static_cast<int>(CVPixelBufferGetWidth(buf));
    const int height  = static_cast<int>(CVPixelBufferGetHeight(buf));
    const int stride  = static_cast<int>(CVPixelBufferGetBytesPerRow(buf));

    // kCVPixelFormatType_32BGRA: byte order B-G-R-A → QImage::Format_ARGB32
    // on little-endian (all Apple Silicon + Intel Macs).
    QImage copy = QImage(data, width, height, stride, QImage::Format_ARGB32).copy();

    CVPixelBufferUnlockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);

    owner->onFrameCaptured(QVideoFrame(copy));
}

@end

// ---------------------------------------------------------------------------
// VideoInputApple
// ---------------------------------------------------------------------------

VideoInputApple::VideoInputApple(QObject *parent)
    : VideoInputBase(parent)
    , d(new VideoInputApplePrivate)
{
}

VideoInputApple::~VideoInputApple()
{
    stop();
    delete d;
}

bool VideoInputApple::start(const QString &deviceId)
{
    stop();

    @autoreleasepool {
        // ── Find device ────────────────────────────────────────────────────
        AVCaptureDevice *device = nil;

        if (deviceId.isEmpty()) {
            device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        } else {
            NSString *needle = deviceId.toNSString();
            NSArray<AVCaptureDeviceType> *types = @[
                AVCaptureDeviceTypeBuiltInWideAngleCamera,
                AVCaptureDeviceTypeExternal
            ];
            AVCaptureDeviceDiscoverySession *disco =
                [AVCaptureDeviceDiscoverySession
                    discoverySessionWithDeviceTypes:types
                                          mediaType:AVMediaTypeVideo
                                           position:AVCaptureDevicePositionUnspecified];
            for (AVCaptureDevice *dev in disco.devices) {
                if ([dev.uniqueID isEqualToString:needle] ||
                    [dev.localizedName isEqualToString:needle]) {
                    device = dev;
                    break;
                }
            }
        }

        if (!device) {
            emit errorOccurred(tr("No camera device available"));
            return false;
        }

        // ── Build session ──────────────────────────────────────────────────
        AVCaptureSession *session = [[AVCaptureSession alloc] init];
        session.sessionPreset = AVCaptureSessionPresetHigh;

        NSError *err = nil;
        AVCaptureDeviceInput *input =
            [AVCaptureDeviceInput deviceInputWithDevice:device error:&err];
        if (!input) {
            emit errorOccurred(tr("Cannot open camera: %1")
                .arg(QString::fromNSString(err.localizedDescription)));
            return false;
        }
        if (![session canAddInput:input]) {
            emit errorOccurred(tr("Cannot add camera input to capture session"));
            return false;
        }
        [session addInput:input];

        // ── Video output ───────────────────────────────────────────────────
        AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
        output.alwaysDiscardsLateVideoFrames = YES;
        output.videoSettings = @{
            (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
        };

        VideoInputAppleDelegate *delegate = [[VideoInputAppleDelegate alloc] init];
        delegate.owner = this;
        [output setSampleBufferDelegate:delegate
                                  queue:dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0)];

        if (![session canAddOutput:output]) {
            delegate.owner = nullptr;
            emit errorOccurred(tr("Cannot add video output to capture session"));
            return false;
        }
        [session addOutput:output];

        [session startRunning];

        d->session  = session;
        d->output   = output;
        d->delegate = delegate;
    }

    m_state = State::Active;
    emit stateChanged(State::Active);
    return true;
}

void VideoInputApple::stop()
{
    if (!d->session)
        return;

    // Nil owner first so in-flight callbacks become no-ops.
    ((VideoInputAppleDelegate *)d->delegate).owner = nullptr;

    [d->session stopRunning];
    [d->output setSampleBufferDelegate:nil queue:nil];

    d->session  = nil;
    d->output   = nil;
    d->delegate = nil;

    if (m_state != State::Stopped) {
        m_state = State::Stopped;
        emit stateChanged(State::Stopped);
    }
}

void VideoInputApple::suspend()
{
    if (d->session && m_state == State::Active) {
        [d->session stopRunning];
        m_state = State::Suspended;
        emit stateChanged(State::Suspended);
    }
}

void VideoInputApple::resume()
{
    if (d->session && m_state == State::Suspended) {
        [d->session startRunning];
        m_state = State::Active;
        emit stateChanged(State::Active);
    }
}

bool VideoInputApple::isActive() const
{
    return m_state == State::Active;
}

QVideoFrameFormat VideoInputApple::frameFormat() const
{
    return QVideoFrameFormat{};
}

void VideoInputApple::onFrameCaptured(const QVideoFrame &frame)
{
    emit videoFrameReady(frame);
}

CameraCapabilities VideoInputApple::queryCapabilities() const
{
    CameraCapabilities caps;
    caps.queriedAt   = QDateTime::currentDateTime();
    caps.driverVersion = "AVFoundation";

    @autoreleasepool {
        AVCaptureDevice *device = nil;

        // Use the already-open device if available, otherwise find the default.
        if (d->session) {
            for (AVCaptureInput *input in d->session.inputs) {
                if ([input isKindOfClass:[AVCaptureDeviceInput class]])
                    device = ((AVCaptureDeviceInput *)input).device;
            }
        }
        if (!device)
            device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        if (!device)
            return caps;

        caps.modelName = QString::fromNSString(device.localizedName);

        // --- Connection interface ---
        if ([device.deviceType isEqualToString:AVCaptureDeviceTypeBuiltInWideAngleCamera]
#if defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
            || [device.deviceType isEqualToString:AVCaptureDeviceTypeBuiltInTelephotoCamera]
#endif
        ) {
            caps.connectionInterface = CameraCapabilities::Interface::CSI2;
        } else {
            caps.connectionInterface = CameraCapabilities::Interface::USB3;
        }

        // --- Pixel formats and resolution presets ---
        caps.pixelFormat.kind     = CapabilityKind::Discrete;
        caps.pixelFormat.writable = true;
        caps.resolution.kind     = CapabilityKind::Discrete;
        caps.resolution.writable = true;

        double minFps = 1e9, maxFps = 0.0;

        for (AVCaptureDeviceFormat *fmt in device.formats) {
            CMFormatDescriptionRef desc = fmt.formatDescription;
            FourCharCode subtype = CMFormatDescriptionGetMediaSubType(desc);
            CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(desc);

            // Pixel format
            PixelFormat pf;
            switch (subtype) {
                case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
                case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
                    pf.encoding     = PixelEncoding::YUV420_NV12;
                    pf.bitsPerPixel = 12;
                    pf.nativeKey    = (subtype == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
                                      ? "420f" : "420v";
                    break;
                case kCMVideoCodecType_JPEG:
                case kCMVideoCodecType_JPEG_OpenDML:
                    pf.encoding     = PixelEncoding::MJPEG;
                    pf.bitsPerPixel = 0;
                    pf.nativeKey    = "jpeg";
                    break;
                case kCVPixelFormatType_32BGRA:
                    pf.encoding     = PixelEncoding::BGR8;
                    pf.bitsPerPixel = 32;
                    pf.nativeKey    = "BGRA";
                    break;
                default: {
                    char fourcc[5] = { (char)(subtype >> 24), (char)(subtype >> 16),
                                       (char)(subtype >> 8),  (char)(subtype), 0 };
                    pf.encoding  = PixelEncoding::Unknown;
                    pf.nativeKey = QString::fromLatin1(fourcc);
                    break;
                }
            }
            bool pfFound = false;
            for (const auto &existing : caps.pixelFormat.supported)
                if (existing.nativeKey == pf.nativeKey) { pfFound = true; break; }
            if (!pfFound) caps.pixelFormat.supported.append(pf);

            // Resolution preset
            Resolution r { (int)dims.width, (int)dims.height };
            bool resFound = false;
            for (const auto &existing : caps.resolution.presets)
                if (existing.width == r.width && existing.height == r.height) { resFound = true; break; }
            if (!resFound) caps.resolution.presets.append(r);

            // Frame rate ranges
            for (AVFrameRateRange *range in fmt.videoSupportedFrameRateRanges) {
                minFps = qMin(minFps, range.minFrameRate);
                maxFps = qMax(maxFps, range.maxFrameRate);
            }
        }

        if (maxFps > 0.0) {
            caps.frameRate.kind               = CapabilityKind::Range;
            caps.frameRate.readable           = true;
            caps.frameRate.writable           = true;
            caps.frameRate.range.min          = minFps;
            caps.frameRate.range.max          = maxFps;
            caps.frameRate.range.step         = 0;
            caps.frameRate.range.defaultValue = maxFps;
        }

        // --- Exposure time (CMTime → microseconds) ---
        CMTime minExp = device.activeFormat.minExposureDuration;
        CMTime maxExp = device.activeFormat.maxExposureDuration;
        double minExpUs = (double)minExp.value / minExp.timescale * 1e6;
        double maxExpUs = (double)maxExp.value / maxExp.timescale * 1e6;
        if (maxExpUs > 0.0) {
            caps.exposureTime.kind               = CapabilityKind::Range;
            caps.exposureTime.readable           = true;
            caps.exposureTime.writable           = true;
            caps.exposureTime.range.min          = minExpUs;
            caps.exposureTime.range.max          = maxExpUs;
            caps.exposureTime.range.step         = 0;
            caps.exposureTime.range.defaultValue = maxExpUs;
        }

        // --- ISO as proxy for gain ---
        float minISO = device.activeFormat.minISO;
        float maxISO = device.activeFormat.maxISO;
        if (maxISO > 0.0f) {
            caps.gain.kind               = CapabilityKind::Range;
            caps.gain.readable           = true;
            caps.gain.writable           = true;
            caps.gain.range.min          = minISO;
            caps.gain.range.max          = maxISO;
            caps.gain.range.step         = 0;
            caps.gain.range.defaultValue = minISO;
        }

        // --- Exposure mode ---
        caps.exposureMode.kind     = CapabilityKind::Discrete;
        caps.exposureMode.readable = true;
        caps.exposureMode.writable = true;
        caps.exposureMode.options  = {
            { "Locked", "Locked", device.exposureMode == AVCaptureExposureModeLocked },
            { "Auto",   "Auto",   device.exposureMode == AVCaptureExposureModeAutoExpose },
            { "Custom", "Custom", device.exposureMode == AVCaptureExposureModeCustom }
        };
    }

    return caps;
}
