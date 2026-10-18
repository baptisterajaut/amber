find_package(Qt6 REQUIRED COMPONENTS Test Core)

set(AMBER_CORE_SOURCES
  ${CMAKE_SOURCE_DIR}/core/alpha.cpp
  ${CMAKE_SOURCE_DIR}/core/alpha.h
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

add_executable(amber-test-shortcutfile
  test_shortcutfile.cpp
  ${CMAKE_SOURCE_DIR}/core/shortcutfile.cpp
  ${CMAKE_SOURCE_DIR}/core/shortcutfile.h
)
target_include_directories(amber-test-shortcutfile PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-shortcutfile PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-shortcutfile COMMAND amber-test-shortcutfile)

add_executable(amber-test-pixel-sampler
  test_pixel_sampler.cpp
  ${CMAKE_SOURCE_DIR}/core/pixelsampler.cpp
  ${CMAKE_SOURCE_DIR}/core/pixelsampler.h
)
target_include_directories(amber-test-pixel-sampler PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-pixel-sampler PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-pixel-sampler COMMAND amber-test-pixel-sampler)

# Graph Editor pure math (core/graphmath)
add_executable(amber-test-graph-view-math
  test_graphviewmath.cpp
  ${CMAKE_SOURCE_DIR}/core/graphmath.cpp
  ${CMAKE_SOURCE_DIR}/core/graphmath.h
)
target_include_directories(amber-test-graph-view-math PRIVATE ${CMAKE_SOURCE_DIR})
# effects/effect.h only for the keyframe types and keyframe_shows_handles(); it includes QRhi and widget headers
target_link_libraries(amber-test-graph-view-math PRIVATE Qt6::Test Qt6::Widgets Qt6::GuiPrivate)
add_test(NAME amber-test-graph-view-math COMMAND amber-test-graph-view-math)

# Footage::decode_url(). footage.cpp needs only PreviewGenerator::cancel(), stubbed in the test; Clip-side export
# mode (OpenedForExport, NeedsCacherReconfigure) needs the engine and is not covered here.
add_executable(amber-test-footage
  test_footage.cpp
  ${CMAKE_SOURCE_DIR}/project/footage.cpp
  ${CMAKE_SOURCE_DIR}/project/footage.h
)
target_include_directories(amber-test-footage PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-footage PRIVATE Qt6::Test Qt6::Widgets Qt6::GuiPrivate FFMPEG::avutil FFMPEG::avcodec FFMPEG::avformat
  FFMPEG::swscale FFMPEG::swresample)
add_test(NAME amber-test-footage COMMAND amber-test-footage)

add_executable(amber-test-config
  test_config.cpp
  ${CMAKE_SOURCE_DIR}/global/config.cpp
  ${CMAKE_SOURCE_DIR}/global/config.h
  ${AMBER_CORE_SOURCES}
)
target_include_directories(amber-test-config PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-config PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-config COMMAND amber-test-config)

# The effect fields' widget wiring that Effect Controls uses for multi-selected clips. Effect, EffectRow and Clip
# pull most of the app, so test_fieldwidgets_stubs.cpp stands in for what the fields and undo commands touch.
add_executable(amber-test-fieldwidgets
  test_fieldwidgets.cpp
  test_fieldwidgets_stubs.cpp
  ${CMAKE_SOURCE_DIR}/effects/effect.h
  ${CMAKE_SOURCE_DIR}/effects/effectrow.h
  ${CMAKE_SOURCE_DIR}/engine/cacher.h
  ${CMAKE_SOURCE_DIR}/effects/effectfield.cpp
  ${CMAKE_SOURCE_DIR}/effects/fields/boolfield.cpp
  ${CMAKE_SOURCE_DIR}/effects/fields/combofield.cpp
  ${CMAKE_SOURCE_DIR}/effects/fields/doublefield.cpp
  ${CMAKE_SOURCE_DIR}/engine/undo/comboaction.cpp
  ${CMAKE_SOURCE_DIR}/engine/undo/undo_effect.cpp
  ${CMAKE_SOURCE_DIR}/engine/undo/undo_generic.cpp
  ${CMAKE_SOURCE_DIR}/engine/undo/undostack.cpp
  ${CMAKE_SOURCE_DIR}/global/config.cpp
  ${CMAKE_SOURCE_DIR}/rendering/clipqueue.cpp
  ${CMAKE_SOURCE_DIR}/ui/comboboxex.cpp
  ${CMAKE_SOURCE_DIR}/ui/labelslider.cpp
  ${CMAKE_SOURCE_DIR}/ui/menu.cpp
  ${CMAKE_SOURCE_DIR}/ui/styling.cpp
  ${AMBER_CORE_SOURCES}
)
target_include_directories(amber-test-fieldwidgets PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-fieldwidgets PRIVATE Qt6::Test Qt6::Widgets Qt6::GuiPrivate FFMPEG::avutil
  FFMPEG::avcodec FFMPEG::avformat FFMPEG::avfilter FFMPEG::swscale FFMPEG::swresample)
set_target_properties(amber-test-fieldwidgets PROPERTIES AUTOMOC ON)
add_test(NAME amber-test-fieldwidgets COMMAND amber-test-fieldwidgets)
set_tests_properties(amber-test-fieldwidgets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
