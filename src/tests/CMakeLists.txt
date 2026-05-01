find_package(Qt6 REQUIRED COMPONENTS Test)

# All test targets link amber-engine which provides core, engine, effects,
# rendering, project, and global sources via OBJECT library.
# test_ui_stubs.cpp provides null implementations for UI-layer symbols that
# amber-engine objects reference but that aren't built for headless tests.

set(AMBER_TEST_STUBS test_ui_stubs.cpp)

add_executable(amber-tests test_math.cpp ${AMBER_TEST_STUBS})
target_include_directories(amber-tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-tests PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-tests COMMAND amber-tests)

add_executable(amber-test-path test_path.cpp ${AMBER_TEST_STUBS})
target_include_directories(amber-test-path PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-path PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-path COMMAND amber-test-path)

add_executable(amber-test-audio test_audio.cpp ${AMBER_TEST_STUBS})
target_include_directories(amber-test-audio PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-audio PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-audio COMMAND amber-test-audio)

add_executable(amber-test-headers test_core_headers.cpp ${AMBER_TEST_STUBS})
target_include_directories(amber-test-headers PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-headers PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-headers COMMAND amber-test-headers)

add_executable(amber-engine-tests test_engine.cpp ${AMBER_TEST_STUBS})
target_include_directories(amber-engine-tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-engine-tests PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-engine-tests COMMAND amber-engine-tests)

add_executable(amber-test-srt test_srt.cpp ${AMBER_TEST_STUBS})
target_include_directories(amber-test-srt PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-srt PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-srt COMMAND amber-test-srt)

add_executable(amber-test-effects test_effects.cpp ${AMBER_TEST_STUBS})
target_include_directories(amber-test-effects PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-effects PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-effects COMMAND amber-test-effects)

add_executable(amber-test-projectio test_projectio.cpp ${AMBER_TEST_STUBS})
target_include_directories(amber-test-projectio PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-projectio PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-projectio COMMAND amber-test-projectio)

# QRhi headless smoke test
add_executable(amber-test-rhi-smoke test_rhi_smoke.cpp ${AMBER_TEST_STUBS})
target_include_directories(amber-test-rhi-smoke PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-rhi-smoke PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-rhi-smoke COMMAND amber-test-rhi-smoke)

# Rendering integration test — exercises QRhi pipeline with real engine types
add_executable(amber-test-rendering test_rendering.cpp ${AMBER_TEST_STUBS})
target_include_directories(amber-test-rendering PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-rendering PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-rendering COMMAND amber-test-rendering)

add_executable(amber-test-timeline-layout
  test_timeline_layout.cpp
  ${CMAKE_SOURCE_DIR}/panels/timeline_layout.cpp
  ${CMAKE_SOURCE_DIR}/panels/timeline_layout.h
)
target_include_directories(amber-test-timeline-layout PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-timeline-layout PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-timeline-layout COMMAND amber-test-timeline-layout)
