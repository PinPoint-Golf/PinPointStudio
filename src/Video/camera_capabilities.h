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
#include <QString>
#include <QList>
#include <QVariantMap>
#include <QDateTime>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

template<typename T>
struct CapabilityRange {
    T   min{};
    T   max{};
    T   step{};          // 0 = continuous
    T   defaultValue{};
};

struct DiscreteOption {
    QString displayName;
    QString key;         // backend-native identifier (GenICam name, V4L2 fourcc, etc.)
    bool    isDefault = false;
};

enum class CapabilityKind {
    Unavailable,   // parameter not supported by this camera
    Fixed,         // supported but not user-controllable
    Range,         // continuous or stepped numeric range
    Discrete       // enumerated set of named options
};

template<typename T>
struct NumericCapability {
    CapabilityKind   kind          = CapabilityKind::Unavailable;
    CapabilityRange<T> range       = {};
    T                fixedValue   = {};
    bool             readable     = false;
    bool             writable     = false;
};

struct EnumCapability {
    CapabilityKind        kind       = CapabilityKind::Unavailable;
    QList<DiscreteOption> options;
    QString               fixedValue;
    bool                  readable   = false;
    bool                  writable   = false;
};

// ---------------------------------------------------------------------------
// Pixel format
// ---------------------------------------------------------------------------

enum class PixelEncoding {
    Unknown,
    Mono8, Mono10, Mono12, Mono16,
    BayerRG8, BayerGB8, BayerGR8, BayerBG8,
    BayerRG16, BayerGB16,
    RGB8, BGR8, RGBA8,
    YUV422_YUYV, YUV422_UYVY, YUV420_NV12, YUV420_I420,
    MJPEG, H264, H265
};

struct PixelFormat {
    PixelEncoding encoding    = PixelEncoding::Unknown;
    int           bitsPerPixel = 0;
    QString       nativeKey;   // e.g. "BayerRG8", "YUYV", "420v"
};

struct PixelFormatCapability {
    CapabilityKind      kind = CapabilityKind::Unavailable;
    QList<PixelFormat>  supported;
    PixelFormat         defaultFormat;
    bool                writable = false;
};

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

struct Resolution {
    int width  = 0;
    int height = 0;
};

struct ResolutionCapability {
    CapabilityKind    kind = CapabilityKind::Unavailable;
    // kind == Range: free width x height within bounds (industrial)
    CapabilityRange<int> widthRange;
    CapabilityRange<int> heightRange;
    // kind == Discrete: fixed presets (consumer / AVFoundation)
    QList<Resolution> presets;
    Resolution        defaultResolution;
    bool              writable = false;
};

// ---------------------------------------------------------------------------
// ROI
// ---------------------------------------------------------------------------

struct ROICapability {
    bool supported = false;
    CapabilityRange<int> offsetXRange;
    CapabilityRange<int> offsetYRange;
    CapabilityRange<int> widthRange;
    CapabilityRange<int> heightRange;
};

// ---------------------------------------------------------------------------
// Trigger / sync
// ---------------------------------------------------------------------------

enum class TriggerSource { None, Software, Hardware, Action };

struct TriggerCapability {
    bool                  supported        = false;
    QList<TriggerSource>  sources;
    bool                  hasHardwareInput = false;
    bool                  hasTimestamping  = false;
};

// ---------------------------------------------------------------------------
// Top-level camera capabilities
// ---------------------------------------------------------------------------

struct CameraCapabilities {

    // --- Identity ---
    QString vendorName;
    QString modelName;
    QString serialNumber;
    QString firmwareVersion;
    QString driverVersion;

    // --- Connection ---
    enum class Interface {
        Unknown, USB2, USB3, GigE, GigE5, GigE10, CSI2, HDMI, Virtual
    };
    Interface connectionInterface = Interface::Unknown;
    double    maxBandwidthMBps    = 0.0;

    // --- Imaging ---
    ResolutionCapability  resolution;
    PixelFormatCapability pixelFormat;

    // Physical sensor size (useful for real-world dimension calculations)
    double sensorWidthMm  = 0.0;
    double sensorHeightMm = 0.0;

    // --- Acquisition ---
    NumericCapability<double> frameRate;    // fps
    NumericCapability<double> exposureTime; // microseconds
    NumericCapability<double> gain;         // dB
    NumericCapability<double> gamma;
    EnumCapability            exposureMode; // e.g. Auto / Manual / Once
    EnumCapability            gainMode;

    // --- Advanced (industrial only) ---
    ROICapability     roi;
    TriggerCapability trigger;

    struct ChunkDataSupport {
        bool frameCounter   = false;
        bool timestamp      = false;
        bool exposureTime   = false;
        bool gain           = false;
        bool sequencerIndex = false;
    } chunkData;

    // --- Lens control ---
    NumericCapability<double> focus;
    NumericCapability<double> zoom;
    bool hasLensControl = false;

    // --- Metadata ---
    QDateTime queriedAt;
    bool      isVirtual = false;

    // Backend-specific extras that don't fit the schema.
    // Keys are namespaced: "spinnaker.DeviceTemperature", "aravis.PacketSize", etc.
    QVariantMap extensions;
};
