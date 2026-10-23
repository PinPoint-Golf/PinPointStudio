# Session report — stills tests (owned by the agent building the stills part). Included from CMakeLists.txt here,
# so ${GUI}, ${SRC} and pp_add_test are in scope.
#
# ReportStills (report/report_stills.h): the segment a line reads for each handedness, the crop maths and its
# privacy bound, the exposure curve, the position instant → frame → pose sample matching, on synthetic data;
# then one real swing READ-ONLY from the share (SKIPs when the share is absent). The links are the minimum the
# stills themselves need: the markup stream rules (markup_truth.cpp, which pulls in pinpoint_swingstore —
# pp_add_test links it), the MP4 reader and OpenCV's video I/O (as mp4_frame_reader_test links them), and the
# app log for ppWarn. `report_stills_test --review <dir>` renders the review set (not run by ctest).
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)
pp_add_test(report_stills_test
    SOURCES report_stills_test.cpp
            ${GUI}/report/report_stills.cpp
            ${GUI}/report/report_stills_pure.cpp
            ${GUI}/markup/markup_truth.cpp
            ${SRC}/Analysis/mp4_frame_reader.cpp
            ${SRC}/Core/pp_log_stream.cpp
            ${SRC}/Core/PpMessageLog.cpp
    INCLUDE ${GUI} ${SRC} ${SRC}/Core ${OpenCV_INCLUDE_DIRS}
    LINK    Qt6::Concurrent ${OpenCV_LIBS}
    AUTOMOC)
