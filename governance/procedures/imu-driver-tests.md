# Procedure: the IMU and analysis unit tests, standalone

`src/Analysis/tests/` is a standalone ctest suite (its own `main()` and `CHECK_NEAR`, no googletest)[^src].

1. Configure and build with an explicit Qt prefix and a bounded job count ([BT-1](../controls/build.md)): `cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=<Qt prefix>`, then `cmake --build build/analyzer-tests -j <n>`.
2. Run through ctest ([BT-3](../controls/test.md)): `ctest --test-dir build/analyzer-tests --output-on-failure`.
3. Choose the cheapest tier that covers the change:
   - header-only goldens (`imu_calibration.h`, `wrist_angles.h`, the Madgwick filter) need only Qt Core and Gui: add the test with `pp_add_test(name srcs...)`;
   - the ESKF filter needs Eigen (from `-DEIGEN_INCLUDE_DIR`, else the app build's fetched copy, else 3.4.0): put `third_party/imu_ekf_compat` before `third_party/imu_ekf` on the include path, and compile only `eskf_orientation_filter.cpp`, since `ESKF.h` includes `ESKF.cpp`;
   - the full motion-sensor driver stack links `imu_test_stubs.cpp`, with per-target `AUTOMOC ON`, C++20 and `Qt6::Bluetooth`.
4. Reach protected or private parsing methods through a test subclass that promotes them with `using`; the keystone calibration golden is `imu_calibration_test.cpp`. The ESKF seeds gravity as 180° about X (not Madgwick's identity): characterise it relative to its own seed.

[^src]: Practice in use since mid-2026 (provisioning the motion-sensor tests). Owner (proposed): Product Owner.
