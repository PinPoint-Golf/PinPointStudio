# IMU

*Directory:* `src/Gui/imu/`  
*3 components · 1,029 lines of QML*

The toolbar IMU device panel, the sensor-mount vocabulary singleton, and the dev-hatch capture page.

**C++ in this area:** `HmUnit`, `HmInstance`, `ImuDeviceBase`, `ImuInstance`, `ImuManager`, `PacedConnectQueue`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [CapturePage](#capturepage) | 496 | `Item` | 1 | 4 |  |
| [ImuMounts](#imumounts) | 87 | `QtObject` | 5 | 0 | singleton |
| [PpImuPanel](#ppimupanel) | 446 | `Item` | 1 | 8 | 2 local comp. |

### CapturePage
`src/Gui/imu/CapturePage.qml` · 496 lines · root `Item` · 2 hard-coded colour(s)

IMU capture page: scan/connect, Viz and Log tabs, Arm and Body 3-D views (persistent ImuVizView/ArmVizView/BodyVizView bound to the first active IMU), Zero and Save Log. Hosted by PlayPage (dev hatch).  
*(description written for this catalogue — the file has no header comment)*

- **Properties:** `QtObject firstInst`, `QtObject vizController`
- **Uses:** `ArmVizView`, `BodyVizView`, `ImuVizView`, `PpComboBox`
- **Used by (1):** `PlayPage`

### ImuMounts
`src/Gui/imu/ImuMounts.qml` · 87 lines · root `QtObject` · **singleton**

> The sensor MOUNTS, by name — the one place a placement role becomes words on screen, and
> the one place that says which mounts the pickers offer (session_wizard_refactor_design.md
> §4.12, §4.13). Every page that shows or asks for a mount reads this: Settings ▸ IMUs, the
> toolbar sensor panel, the arm view's legend, and the setup pages after them.
> Roles are the SETTINGS spelling (AppSettings::imuRoles, segment_role.h placementRoleName):
> "leadForearm", "leadHand", "leadUpperArm", "pelvis", "thorax". No slot letters: the letters
> survive only as the legacy imuPlacement view, which this file never reads.

- **Properties:** `readonly var armRoles`, `readonly var trunkRoles`, `readonly var allRoles`, `readonly string hackMotionLabel`, `readonly string unassignedLabel`, `bool trunkMountsOffered`, `readonly var offeredRoles`
- **Functions:** `isOffered(role)`, `label(role)`, `deviceMountLabel(isHackMotion, role)`
- **Used by (5):** `ArmVizView`, `ImusPage`, `ImusPanel`, `PpImuPanel`, `SetupContext`
- **Exercised by tests/tools:** `setup_mounts_settings`, `tst_setup_mounts`

### PpImuPanel
`src/Gui/imu/PpImuPanel.qml` · 446 lines · root `Item` · 2 hard-coded colour(s)

> IMU device panel for the session toolbar. Two modes:
> "list"      — scoped action row (Scan / Connect / Calibrate) + device rows.
> Each row leads with the device vocabulary's badge (a check when
> connected and calibrated, a colorAttention target when it needs
> calibration, a colorError target when it failed, the dashed ring
> when not connected) and says the same in words. The Calibrate
> action is drawn in colorAttention while there is work to do.
> "calibrate" — hosts ImuCalibrationFlow compactly IN-PANEL; the panel grows to
> fit it. Calibration NEVER leaves this panel / the Wrist screen.
> The heading turns colorAttention, and the toolbar turns the
> popover card's rule the same tone.
> The attention marks live ONLY around the flow — never inside it.

- **Properties:** `string mode`, `readonly bool needsCalibration`, `readonly bool allConnected`
- **Functions:** `disconnectAll()`, `startConnect()`
- **Local components:** `ScopedAction`, `ImuRow`
- **Uses:** `ImuCalibrationFlow`, `ImuMounts`, `PpBadge`, `PpCardNote`, `PpChip`, `PpConnectingFrame`, `PpMicro`, `PpPressable`
- **Used by (1):** `PpSessionToolbar`
- **Exercised by tests/tools:** `CalibDriver`, `tst_setup_rolesviz`
