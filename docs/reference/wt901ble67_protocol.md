# WT901BLE67 Protocol Reference

**Authority:** WitmotionSDK C# source (`Bwt901bleProcessor.cs`, `Bwt901bleResolver.cs`, `BWT901BLE.cs`)  
**Secondary:** WIT Standard Communication Protocol PDF  
**Implementation:** PinPoint Studio `src/IMU/wt9011dcl_base.cpp`, `wt9011dcl_ble.cpp`

Where the SDK and protocol doc disagree, the SDK is correct. Confirmed discrepancies are called out explicitly.

---

## BLE Transport

The WT901BLE67 exposes a custom UART-bridge GATT service. All communication — both commands to the device and data from it — passes through two characteristics on this service.

**Service UUID:** `0000ffe5-0000-1000-8000-xxxxxxxxxx`  
**Write characteristic (host → device):** `0000ffe9-0000-1000-8000-xxxxxxxxxx`  
**Notify characteristic (device → host):** `0000ffe4-0000-1000-8000-xxxxxxxxxx`

The base UUID suffix varies between hardware revisions: `00805f9b34fb` on some units, `00805f9a34fb` on the WT901BLE67. Always search by the 16-bit service ID (`ffe5`) as a substring match rather than an exact 128-bit comparison, then derive the characteristic UUIDs from whichever base was discovered.

**Write mode:** Write Without Response (no ACK).  
**Notifications:** Enable the CCCD descriptor (`0x0100` for Notify, `0x0200` for Indicate) on the notify characteristic before expecting data. On macOS this is asynchronous — wait for `descriptorWritten` confirmation before issuing init commands.

**No BLE Battery Service (0x180F)** is present on this device.

---

## Command Format

Every command sent to the device is exactly **5 bytes**:

```
FF  AA  [REG]  [LO]  [HI]
```

This writes the 16-bit value `(HI << 8) | LO` to register `REG`. All values are little-endian.

### Unlock

The device auto-locks after 10 seconds of serial-port inactivity (protocol doc note — applies to UART mode). For **BLE**, writes to configuration registers require an unlock first, but register reads (`READADDR`) do not.

**Unlock command:** `FF AA 69 88 B5`  
(Writes value `0xB588` to KEY register `0x69`)

Send unlock immediately before any configuration write. The device does not require a wait after unlock before accepting the next command over BLE.

### Save

To persist configuration changes to NVM: `FF AA 00 00 00`

PinPoint Studio does **not** save after zeroing — the transient state is intentional so the device reverts to hardware defaults on next power-on.

---

## Configuration Registers (Write)

All written as: `FF AA [REG] [LO] [HI]`

| Hex  | Dec | Name        | Description                     | Values used in PinPoint Studio          |
|------|-----|-------------|----------------------------------|----------------------------------|
| 0x00 | 0   | SAVE        | Save to NVM / reboot/reset      | `0x0000` = save                  |
| 0x01 | 1   | CALSW       | Calibration mode                | see CALSW values below           |
| 0x02 | 2   | RSW         | Output content selection        | ⚠ do not call on WT901BLE67      |
| 0x03 | 3   | RRATE       | Output rate                     | see RRATE values below           |
| 0x04 | 4   | BAUD        | Serial baud rate                | (serial only, ignored over BLE)  |
| 0x05 | 5   | AXOFFSET    | Acceleration X bias             |                                  |
| 0x06 | 6   | AYOFFSET    | Acceleration Y bias             |                                  |
| 0x07 | 7   | AZOFFSET    | Acceleration Z bias             |                                  |
| 0x08 | 8   | GXOFFSET    | Angular velocity X bias         |                                  |
| 0x09 | 9   | GYOFFSET    | Angular velocity Y bias         |                                  |
| 0x0A | 10  | GZOFFSET    | Angular velocity Z bias         |                                  |
| 0x0B | 11  | HXOFFSET    | Magnetic field X bias           |                                  |
| 0x0C | 12  | HYOFFSET    | Magnetic field Y bias           |                                  |
| 0x0D | 13  | HZOFFSET    | Magnetic field Z bias           |                                  |
| 0x0E | 14  | D0MODE      | D0 pin mode                     |                                  |
| 0x0F | 15  | D1MODE      | D1 pin mode                     |                                  |
| 0x10 | 16  | D2MODE      | D2 pin mode                     |                                  |
| 0x11 | 17  | D3MODE      | D3 pin mode                     |                                  |
| 0x1A | 26  | IICADDR     | I²C device address              |                                  |
| 0x1B | 27  | LEDOFF      | Turn off LED                    |                                  |
| 0x1C | 28  | MAGRANGX    | Mag field X calibration range   |                                  |
| 0x1D | 29  | MAGRANGY    | Mag field Y calibration range   |                                  |
| 0x1E | 30  | MAGRANGZ    | Mag field Z calibration range   |                                  |
| 0x1F | 31  | BANDWIDTH   | Sensor bandwidth                | SDK: `0x04`=20 Hz, `0x00`=256 Hz |
| 0x20 | 32  | GYRORANGE   | Gyroscope range                 |                                  |
| 0x21 | 33  | ACCRANGE    | Accelerometer range             |                                  |
| 0x22 | 34  | SLEEP       | Sleep mode                      |                                  |
| 0x23 | 35  | ORIENT      | Installation direction          | `0x0000`=horizontal, `0x0001`=vertical |
| 0x24 | 36  | AXIS6       | Fusion algorithm                | `0x0000`=9-axis, `0x0001`=6-axis |
| 0x25 | 37  | FILTK       | Dynamic filtering coefficient   |                                  |
| 0x26 | 38  | GPSBAUD     | GPS baud rate                   |                                  |
| 0x27 | 39  | READADDR    | Register read trigger           | see Register Read section        |
| 0x2A | 42  | ACCFILT     | Acceleration filtering          |                                  |
| 0x2D | 45  | POWONSEND   | Power-on command start          |                                  |
| 0x59 | 89  | DELAYT      | Alarm delay time                |                                  |
| 0x5A | 90  | XMIN        | X-axis angle alarm minimum      |                                  |
| 0x5B | 91  | XMAX        | X-axis angle alarm maximum      |                                  |
| 0x5D | 93  | ALARMPIN    | Alarm pin mapping               |                                  |
| 0x5E | 94  | YMIN        | Y-axis alarm minimum            |                                  |
| 0x5F | 95  | YMAX        | Y-axis alarm maximum            |                                  |
| 0x61 | 97  | GYROCALITHR | Gyro still threshold            | default = 500                    |
| 0x62 | 98  | ALARMLEVEL  | Angle alarm level               |                                  |
| 0x63 | 99  | GYROCALTIME | Gyro auto-calibration time      |                                  |
| 0x68 | 104 | TRIGTIME    | Alarm continuous trigger time   |                                  |
| 0x69 | 105 | KEY         | Unlock register                 | write `0xB588` to unlock         |
| 0x6B | 107 | TIMEZONE    | GPS timezone                    |                                  |
| 0x6E | 110 | WZTIME      | Angular velocity rest time      |                                  |
| 0x6F | 111 | WZSTATIC    | Angular velocity static thresh  |                                  |
| 0x74 | 116 | MODDELAY    | 485 data response delay         |                                  |

### CALSW Values

| Value  | Effect |
|--------|--------|
| 0x0000 | Normal working mode (end calibration) |
| 0x0001 | Accelerometer calibration — place flat and still, hold for ~5 s |
| 0x0004 | Zero heading (yaw) to current orientation |
| 0x0007 | Start magnetometer calibration — rotate through all orientations for ~30 s |
| 0x0008 | Zero roll and pitch to current physical position |

### ORIENT Values

| Value  | Meaning |
|--------|---------|
| 0x0000 | Horizontal installation (device lying flat) |
| 0x0001 | Vertical installation (device mounted on a shaft or pole) |

### AXIS6 Values

| Value  | Meaning |
|--------|---------|
| 0x0000 | 9-axis fusion (gyro + accelerometer + magnetometer) |
| 0x0001 | 6-axis fusion (gyro + accelerometer only, no magnetometer) |

6-axis is preferred for fast dynamic motion (e.g. golf swing) where the magnetometer introduces latency and distortion. See [6-Axis vs 9-Axis Fusion Mode](#6-axis-vs-9-axis-fusion-mode) for the full picture including how to switch modes permanently and why quaternion polling is not used.

### RRATE Values (confirmed from protocol spec)

| Value | Rate |
|-------|------|
| 0x01  | 0.2 Hz |
| 0x02  | 0.5 Hz |
| 0x03  | 1 Hz |
| 0x04  | 2 Hz |
| 0x05  | 5 Hz |
| 0x06  | 10 Hz |
| 0x07  | 20 Hz |
| 0x08  | 50 Hz |
| 0x09  | 100 Hz |
| 0x0A  | **undefined/reserved** (gap — do not use) |
| 0x0B  | 200 Hz |
| 0x0C  | Single return |
| 0x0D  | No return (off) |

---

## Read-Only Data Registers

These registers hold sensor output. Access them via the READADDR mechanism (section below). All values are signed int16 little-endian.

| Hex  | Dec | Name        | Description |
|------|-----|-------------|-------------|
| 0x2E | 46  | VERSION     | Firmware version |
| 0x2F | 47  | —           | Firmware version high word |
| 0x30 | 48  | YYMM        | Year (low byte), Month (high byte) |
| 0x31 | 49  | DDHH        | Day (low byte), Hour (high byte) |
| 0x32 | 50  | MMSS        | Minute (low byte), Second (high byte) |
| 0x33 | 51  | MS          | Milliseconds |
| 0x34 | 52  | AX          | Acceleration X |
| 0x35 | 53  | AY          | Acceleration Y |
| 0x36 | 54  | AZ          | Acceleration Z |
| 0x37 | 55  | GX          | Angular velocity X |
| 0x38 | 56  | GY          | Angular velocity Y |
| 0x39 | 57  | GZ          | Angular velocity Z |
| 0x3A | 58  | Roll        | Roll angle |
| 0x3B | 59  | Pitch       | Pitch angle |
| 0x3C | 60  | Yaw         | Heading |
| 0x3D | 61  | HX          | Magnetic field X |
| 0x3E | 62  | HY          | Magnetic field Y |
| 0x3F | 63  | HZ          | Magnetic field Z |
| 0x40 | 64  | TEMP        | Temperature |
| 0x41 | 65  | D0Status    | D0 pin state |
| 0x42 | 66  | D1Status    | D1 pin state |
| 0x43 | 67  | D2Status    | D2 pin state |
| 0x44 | 68  | D3Status    | D3 pin state |
| 0x45 | 69  | PressureL   | Air pressure low 16 bits |
| 0x46 | 70  | PressureH   | Air pressure high 16 bits |
| 0x47 | 71  | HeightL     | Altitude lower 16 bits |
| 0x48 | 72  | HeightH     | Altitude upper 16 bits |
| 0x49 | 73  | LonL        | Longitude lower 16 bits |
| 0x4A | 74  | LonH        | Longitude upper 16 bits |
| 0x4B | 75  | LatL        | Latitude lower 16 bits |
| 0x4C | 76  | LatH        | Latitude upper 16 bits |
| 0x4D | 77  | GPSHeight   | GPS altitude |
| 0x4E | 78  | GPSYAW      | GPS heading |
| 0x4F | 79  | GPSVL       | GPS ground speed low 16 bits |
| 0x50 | 80  | GPSVH       | GPS ground speed high 16 bits |
| 0x51 | 81  | q0          | Quaternion W |
| 0x52 | 82  | q1          | Quaternion X |
| 0x53 | 83  | q2          | Quaternion Y |
| 0x54 | 84  | q3          | Quaternion Z |
| 0x55 | 85  | SVNUM       | Number of GPS satellites |
| 0x56 | 86  | PDOP        | GPS position accuracy |
| 0x57 | 87  | HDOP        | GPS horizontal accuracy |
| 0x58 | 88  | VDOP        | GPS vertical accuracy |
| 0x5C | 92  | BATVAL      | Supply voltage (PDF) — **returns 0 on WT901BLE67; use 0x64 instead** |
| 0x64 | 100 | (unnamed)   | **Battery voltage on WT901BLE67** — SDK confirmed, PDF does not list this |
| 0x6A | 106 | WERROR      | Gyro change indicator |
| 0x72 | 114 | MagType     | Magnetometer type/range — must read before interpreting HX/HY/HZ |
| 0x79 | 121 | XREFROLL    | Roll angle zero reference value |
| 0x7A | 122 | YREFPITCH   | Pitch angle zero reference value |
| 0x7F | 127 | NUMBERID1   | Serial number bytes 1–2 |
| 0x80 | 128 | NUMBERID2   | Serial number bytes 3–4 |
| 0x81 | 129 | NUMBERID3   | Serial number bytes 5–6 |
| 0x82 | 130 | NUMBERID4   | Serial number bytes 7–8 |
| 0x83 | 131 | NUMBERID5   | Serial number bytes 9–10 |
| 0x84 | 132 | NUMBERID6   | Serial number bytes 11–12 |

---

## Register Read Mechanism

To read any register, write its address to the READADDR register (`0x27`):

```
FF  AA  27  [regAddr]  00
```

The high byte must be `0x00`. The device responds asynchronously with a **0x71 frame** (WT901BLE67-specific — the protocol doc incorrectly states the response type is `0x5F`).

---

## Frame Formats

All frames begin with `0x55`. There are three categories: standard checksum'd frames, and two BLE-specific unchecksum'd extensions.

### Standard Frames (11 bytes, with checksum)

```
Byte 0:   0x55 (header)
Byte 1:   TYPE
Bytes 2–9: 4 × signed int16 little-endian (DATA1..DATA4)
Byte 10:  SUMCRC = (0x55 + TYPE + bytes[2..9]) & 0xFF
```

| TYPE | Content |
|------|---------|
| 0x50 | Time |
| 0x51 | Acceleration (XYZ + temperature) |
| 0x52 | Angular velocity (XYZ + voltage¹) |
| 0x53 | Euler angles (Roll/Pitch/Yaw + version) |
| 0x54 | Magnetic field (XYZ + temperature) |
| 0x55 | Port status (D0–D3) |
| 0x56 | Barometric altitude |
| 0x57 | Latitude and longitude |
| 0x58 | Ground speed |
| 0x59 | Quaternion |
| 0x5A | GPS location accuracy |
| 0x5F | Register read response (standard devices only — **not used on WT901BLE67**) |

¹ The PDF notes that VolL/VolH in the 0x52 packet are "invalid for non-Bluetooth products". On BT products they are valid: `Voltage = ((VolH<<8)|VolL) / 100` V. However, the WT901BLE67 outputs 0x61 combined frames by default and does not normally emit 0x52 packets.

#### 0x51 Acceleration Frame

| Bytes | Field | Formula |
|-------|-------|---------|
| 2–3   | AxL/AxH | `Ax = ((AxH<<8)|AxL) / 32768 × 16` g |
| 4–5   | AyL/AyH | `Ay = ((AyH<<8)|AyL) / 32768 × 16` g |
| 6–7   | AzL/AzH | `Az = ((AzH<<8)|AzL) / 32768 × 16` g |
| 8–9   | TL/TH   | `T = ((TH<<8)|TL) / 100` °C |

#### 0x52 Angular Velocity Frame

| Bytes | Field | Formula |
|-------|-------|---------|
| 2–3   | WxL/WxH | `Wx = ((WxH<<8)|WxL) / 32768 × 2000` °/s |
| 4–5   | WyL/WyH | `Wy = ((WyH<<8)|WyL) / 32768 × 2000` °/s |
| 6–7   | WzL/WzH | `Wz = ((WzH<<8)|WzL) / 32768 × 2000` °/s |
| 8–9   | VolL/VolH | `V = ((VolH<<8)|VolL) / 100` V (BT products only) |

#### 0x53 Euler Angle Frame

| Bytes | Field | Formula |
|-------|-------|---------|
| 2–3   | RollL/H  | `Roll  = ((RollH<<8)|RollL) / 32768 × 180` ° |
| 4–5   | PitchL/H | `Pitch = ((PitchH<<8)|PitchL) / 32768 × 180` ° |
| 6–7   | YawL/H   | `Yaw   = ((YawH<<8)|YawL) / 32768 × 180` ° |
| 8–9   | VL/VH    | `Version = (VH<<8)|VL` |

#### 0x54 Magnetic Field Frame

| Bytes | Field | Formula |
|-------|-------|---------|
| 2–3   | HxL/HxH | raw value (scaling depends on mag type, see below) |
| 4–5   | HyL/HyH | raw value |
| 6–7   | HzL/HzH | raw value |
| 8–9   | TL/TH   | `T = ((TH<<8)|TL) / 100` °C |

**Magnetometer scaling** — read register 0x72 first to determine type:

| Type value | Scale factor |
|------------|-------------|
| 2          | × 0.15 μT   |
| 3          | × 0.013 μT  |
| 4          | × 0.058 μT  |
| 5          | × 0.098 μT  |
| 6          | ÷ 120 μT    |
| 7          | × 0.020 μT  |

#### 0x59 Quaternion Frame

| Bytes | Field | Formula |
|-------|-------|---------|
| 2–3   | q0L/H | `q0 = ((q0H<<8)|q0L) / 32768` |
| 4–5   | q1L/H | `q1 = ((q1H<<8)|q1L) / 32768` |
| 6–7   | q2L/H | `q2 = ((q2H<<8)|q2L) / 32768` |
| 8–9   | q3L/H | `q3 = ((q3H<<8)|q3L) / 32768` |

---

### 0x61 — BLE Combined Frame (20 bytes, NO checksum)

WT901BLE67 firmware extension — **not in the protocol doc**. This is the primary streaming frame in BLE mode, sent at the configured output rate. It bundles accel, gyro, and Euler angles in one efficient packet.

```
Byte 0:     0x55
Byte 1:     0x61
Bytes 2–3:  Accel X  (int16 LE)
Bytes 4–5:  Accel Y  (int16 LE)
Bytes 6–7:  Accel Z  (int16 LE)
Bytes 8–9:  Gyro X   (int16 LE)
Bytes 10–11: Gyro Y  (int16 LE)
Bytes 12–13: Gyro Z  (int16 LE)
Bytes 14–15: Roll    (int16 LE)
Bytes 16–17: Pitch   (int16 LE)
Bytes 18–19: Yaw     (int16 LE)
```

Same scaling as the equivalent standard frames:
- Accel: `raw / 32768 × 16` g
- Gyro: `raw / 32768 × 2000` °/s
- Euler: `raw / 32768 × 180` °

**⚠ Critical:** Calling `setOutputData()` (writing to RSW register 0x02) disrupts this frame and must never be done on the WT901BLE67 in streaming mode. Does not include quaternion or voltage data.

---

### 0x71 — Register Read Response (20 bytes, NO checksum)

WT901BLE67 firmware extension — **not in the protocol doc** (which incorrectly states `0x5F` as the read response type). Sent by the device in response to a READADDR write.

```
Byte 0:     0x55
Byte 1:     0x71
Bytes 2–3:  startReg (int16 LE) — echoes the register address that was requested
Bytes 4–5:  Register[startReg+0] value (int16 LE)
Bytes 6–7:  Register[startReg+1] value (int16 LE)
Bytes 8–9:  Register[startReg+2] value (int16 LE)
Bytes 10–11: Register[startReg+3] value (int16 LE)
Bytes 12–19: Additional register values / padding
```

The device always returns a fixed 20-byte block regardless of how many registers were requested. The first register value is at bytes [4:5].

---

## Battery Level (WT901BLE67 specific)

**⚠ The PDF lists register 0x5C (decimal 92) as BATVAL. This returns 0 on the WT901BLE67.** The actual battery register on this device is **0x64 (decimal 100)**, confirmed from WitmotionSDK source (`Bwt901bleProcessor.cs`).

Do not confuse:
- Register `0x40` (decimal 64) = TEMP (temperature) — listed in the PDF
- Register `0x64` (decimal 100) = battery voltage — used by SDK, not in PDF

**Command:** `FF AA 27 64 00`

**Response:** 0x71 frame; BATVAL is at bytes [4:5] (int16 LE). Units: 0.01 V (raw value 370 = 3.70 V).

**Percentage lookup table** (from `Bwt901bleProcessor.cs`):

| Raw value | Percentage |
|-----------|-----------|
| ≥ 396     | 100%       |
| ≥ 393     | 90%        |
| ≥ 387     | 75%        |
| ≥ 382     | 60%        |
| ≥ 379     | 50%        |
| ≥ 377     | 40%        |
| ≥ 373     | 30%        |
| ≥ 370     | 20%        |
| ≥ 368     | 15%        |
| ≥ 350     | 10%        |
| ≥ 340     | 5%         |
| < 340     | 0%         |

The device may return raw = 0 briefly after connection (ADC not ready). Retry after a few seconds. Poll every ~60 seconds during a session.

---

## PinPoint Studio Initialisation Sequence

Sent on every connect and on every `reinitialize()` call (rate change, zero button). No unlock or save is used — settings are intentionally transient.

```
FF AA 23 01 00   ORIENT = 0x0001  vertical installation
FF AA 24 01 00   AXIS6  = 0x0001  6-axis algorithm (gyro only)
FF AA 03 NN 00   RRATE  = NN      output rate (default 0x09 = 100 Hz)
FF AA 01 08 00   CALSW  = 0x0008  zero roll/pitch to current position
FF AA 01 04 00   CALSW  = 0x0004  zero heading
FF AA 01 00 00   CALSW  = 0x0000  return to normal mode
```

After this sequence, the device streams 0x61 combined frames at the configured rate.

**Why no save:** Configuration is re-applied on every connect, so persisting to NVM would provide no benefit and would unnecessarily wear flash.

**Why no unlock:** CALSW, ORIENT, AXIS6, and RRATE writes are accepted without unlock over BLE on this firmware.

---

## 6-Axis vs 9-Axis Fusion Mode

The AXIS6 register (`0x24`) controls the sensor fusion algorithm running inside the device.

| Value  | Mode   | Sensors used                         | Output                      |
|--------|--------|--------------------------------------|-----------------------------|
| 0x0001 | 6-axis | Gyroscope + accelerometer only       | Orientation (Euler / quat)  |
| 0x0000 | 9-axis | Gyroscope + accelerometer + magnetometer | Orientation (Euler / quat) |

**PinPoint Studio uses 6-axis (0x0001).** For fast dynamic motion such as a golf swing the magnetometer introduces lag and is susceptible to distortion from the club shaft and nearby metal. 6-axis gyro-integration is more responsive and more accurate over the short duration of a swing.

### Switching algorithm mode permanently

To change the mode so it persists across power cycles, an explicit **Unlock → Write → Save** sequence is required. This differs from the PinPoint Studio init sequence, which writes AXIS6 without unlock or save (transient, effective until power-off):

```
FF AA 69 88 B5   KEY    = 0xB588   unlock configuration registers
FF AA 24 01 00   AXIS6  = 0x0001   set 6-axis mode  (use 0x0000 for 9-axis)
FF AA 00 00 00   SAVE   = 0x0000   persist to NVM
```

The PinPoint Studio driver does not save this setting — it re-applies AXIS6 on every connect, so NVM persistence provides no benefit and would unnecessarily wear flash.

### Data output in each mode

The 0x61 streaming frame (the BLE push data) always contains **Euler angles**, regardless of which fusion mode is selected. There is no mechanism to replace the Euler fields with quaternion fields in the 0x61 frame on the WT901BLE67 firmware.

> **⚠ RSW register (0x02) does not help here.** Writing to RSW to select quaternion output (bit 7) disrupts the 0x61 combined-frame stream on BLE5 firmware without substituting it with 0x59 quaternion packets. The RSW approach was tested and found to silence the device. It must not be used.

### Obtaining native quaternions — polling via READADDR

Quaternion values are available as read-only registers `0x51–0x54` (Q0–Q3). They can be read on demand by writing to the READADDR register:

**Command:** `FF AA 27 51 00`

**Response:** 0x71 frame; starting register `0x51` is echoed at bytes [2:3]. Quaternion values follow at bytes [4:11]:

```
Bytes [4:5]   Q0 (W)   int16 LE → q0 / 32768.0
Bytes [6:7]   Q1 (X)   int16 LE → q1 / 32768.0
Bytes [8:9]   Q2 (Y)   int16 LE → q2 / 32768.0
Bytes [10:11] Q3 (Z)   int16 LE → q3 / 32768.0
```

Parsing example:
```cpp
if (data[0] == 0x55 && data[1] == 0x71 && data[2] == 0x51) {
    float q0 = (int16_t)(data[5] << 8 | data[4]) / 32768.0f;  // W
    float q1 = (int16_t)(data[7] << 8 | data[6]) / 32768.0f;  // X
    float q2 = (int16_t)(data[9] << 8 | data[8]) / 32768.0f;  // Y
    float q3 = (int16_t)(data[11]<< 8 | data[10])/ 32768.0f;  // Z
}
```

**PinPoint Studio does not implement quaternion polling.** Each poll is a BLE round-trip (write + async notify response). Sustained at 100 Hz — the rate required for golf swing analysis — this would require a write every 10 ms and compete with the device's own streaming traffic, producing unreliable timing and significant BLE bus overhead. The 0x61 streaming frame already delivers data at a guaranteed 100 Hz; PinPoint Studio converts the Euler fields to a quaternion in the driver (`eulerToQuat()`) immediately on receipt and discards the Euler representation. All code above the driver works exclusively in quaternion space.

### Register summary

| Register | Purpose         | Values                                        |
|----------|-----------------|-----------------------------------------------|
| `0x69`   | KEY (unlock)    | `0xB588` = unlock; required before NVM writes |
| `0x00`   | SAVE            | `0x0000` = persist current config to NVM      |
| `0x24`   | AXIS6 (algorithm) | `0x0000` = 9-axis, `0x0001` = 6-axis        |
| `0x02`   | RSW (output)    | ⚠ do not use on WT901BLE67 BLE firmware       |
| `0x51`   | q0 (W)          | Read-only quaternion register (poll via READADDR) |
| `0x52`   | q1 (X)          | Read-only                                     |
| `0x53`   | q2 (Y)          | Read-only                                     |
| `0x54`   | q3 (Z)          | Read-only                                     |

---

## WT901BLE67 Axis Mapping

The 0x61 frame labels axes differently from the standard RPY convention. Confirmed experimentally:

- Roll  → X axis
- Yaw   → Y axis  
- −Pitch → Z axis (pitch is negated)

This mapping is baked into `WT9011DCL_BLE::eulerToQuat()`. All corrections live in the driver layer; nothing above it needs to be aware of the remapping.

---

## Known Discrepancies Between PDF and WT901BLE67 Firmware

| Topic | Protocol PDF says | WT901BLE67 reality |
|-------|-------------------|-------------------|
| Battery register | 0x5C (BATVAL) | 0x64 (unnamed in PDF); 0x5C returns 0 |
| Read response frame type | 0x5F | 0x71 |
| Streaming frame | standard 0x51/0x52/0x53 packets | 0x61 combined frame (BLE extension) |
| Angular velocity voltage | valid for BT products in 0x52 | not available; device uses 0x61 which has no voltage field |
| Command lock | 10 s serial-port auto-lock | No lock over BLE for sensor init commands |
| BLE advertising | Publishes live advertisements (negative RSSI) | Never advertises; only present as OS BLE cache entry (RSSI = 0) |

---

## BLE Connection Notes (Linux / BlueZ)

- Device MAC: `DC:78:5B:33:7A:80` (static random — top 2 bits of `DC` = `11`)
- Filter scan results: only connect to devices whose name starts with `WT901` or whose service UUID contains `ffe5`
- **⚠ The WT901BLE67 does not publish live BLE advertisements.** The device is only present in the OS BLE cache (RSSI = 0). Do not apply an RSSI < 0 filter — it will permanently exclude this device. Connect directly from the cache entry.
- After a failed connection attempt, the device enters a ~40-second cooldown before it will accept a new connection
- `UnknownError (error 1)` after ~25 s = HCI timeout; device is in cooldown
- `UnknownError (error 1)` after < 1 s = BlueZ/kernel still recovering; wait ~15 s before retrying
- `UnknownRemoteDeviceError (error 2)` = device not in BlueZ D-Bus managed objects; rescan required

---

## SDK Reference Files

| File | Content |
|------|---------|
| `Windows_C#/Wit.Example_BWT901BLE/ble5/BWT901BLE.cs` | All commands: unlock, save, calibrate, set rate |
| `Windows_C#/Wit.Example_BWT901BLE/ble5/Components/Bwt901bleResolver.cs` | Frame parsing (0x61 and 0x71) |
| `Windows_C#/Wit.Example_BWT901BLE/ble5/Components/Bwt901bleProcessor.cs` | Data scaling, battery lookup table, init sequence |
| `Windows_C#/Wit.Example_BWT901BLE/WitSdk/Tools/Device/Utils/WitProtocolUtils.cs` | Command builders |
| `Windows_C#/Wit.Example_BWT901BLE/WitSdk/Tools/Device/Utils/DipSensorMagHelper.cs` | Magnetometer type/scaling lookup |
| `Windows_C#/Wit.Example_BWT901BLE/WitSdk/WinBlue/Entity/WinBleOption.cs` | BLE service/characteristic UUIDs |
