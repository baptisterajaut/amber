find_package(Qt6 REQUIRED COMPONENTS Test Core)

set(AMBER_CORE_SOURCES
  ${CMAKE_SOURCE_DIR}/core/audio.cpp
  ${CMAKE_SOURCE_DIR}/core/audio.h
  ${CMAKE_SOURCE_DIR}/core/math.cpp
  ${CMAKE_SOURCE_DIR}/core/math.h
  ${CMAKE_SOURCE_DIR}/core/path.cpp
  ${CMAKE_SOURCE_DIR}/core/path.h
  ${CMAKE_SOURCE_DIR}/core/appcontext.cpp
  ${CMAKE_SOURCE_DIR}/core/appcontext.h
)

add_executable(amber-tests
  test_math.cpp
  ${AMBER_CORE_SOURCES}
)

target_include_directories(amber-tests PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(amber-tests PRIVATE
  Qt6::Test
  Qt6::Core
)

add_test(NAME amber-tests COMMAND amber-tests)

add_executable(amber-test-path
  test_path.cpp
  ${AMBER_CORE_SOURCES}
)
target_include_directories(amber-test-path PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-path PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-path COMMAND amber-test-path)

add_executable(amber-test-audio
  test_audio.cpp
  ${AMBER_CORE_SOURCES}
)
target_include_directories(amber-test-audio PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-audio PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-audio COMMAND amber-test-audio)

add_executable(amber-test-headers
  test_core_headers.cpp
  ${AMBER_CORE_SOURCES}
)
target_include_directories(amber-test-headers PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-headers PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-headers COMMAND amber-test-headers)

# Engine tests — standalone reimplementations of engine algorithms.
# Engine types (Clip, Sequence) cannot be compiled in isolation yet because
# they depend on Cacher/FFmpeg/QRhi/Effects/Config.  These tests verify the
# pure-math algorithms extracted from engine code.  Full integration tests
# will require an OBJECT library refactor (see ROADMAP.md).
add_executable(amber-engine-tests
  test_engine.cpp
)
target_include_directories(amber-engine-tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-engine-tests PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-engine-tests COMMAND amber-engine-tests)

add_executable(amber-test-srt
  test_srt.cpp
  ${CMAKE_SOURCE_DIR}/effects/internal/srtparser.cpp
  ${CMAKE_SOURCE_DIR}/effects/internal/srtparser.h
)
target_include_directories(amber-test-srt PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-srt PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-srt COMMAND amber-test-srt)

add_executable(amber-test-recordingtap
  test_recordingtap.cpp
  ${CMAKE_SOURCE_DIR}/rendering/recordingtap.cpp
  ${CMAKE_SOURCE_DIR}/rendering/recordingtap.h
)
target_include_directories(amber-test-recordingtap PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-recordingtap PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-recordingtap COMMAND amber-test-recordingtap)

add_executable(amber-test-timeline-layout
  test_timeline_layout.cpp
  ${CMAKE_SOURCE_DIR}/panels/timeline_layout.cpp
  ${CMAKE_SOURCE_DIR}/panels/timeline_layout.h
)
target_include_directories(amber-test-timeline-layout PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-timeline-layout PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-timeline-layout COMMAND amber-test-timeline-layout)
