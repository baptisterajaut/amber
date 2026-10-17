find_package(Qt6 REQUIRED COMPONENTS Test)

# All test targets link amber-engine which provides core, engine, effects,
# rendering, project, and global sources via OBJECT library. amber-engine
# references no UI symbol and pulls no QtWidgets, so no test links UI code, except amber-test-scopewidget,
# amber-test-curveeditor and amber-test-fieldwidgets, which compile the self-contained ui/scopewidget.cpp (QPainter
# drawing over core/scopes), ui/curveeditor.cpp (over core/curves) and a few effect field widgets into their own
# executables.

add_executable(amber-tests test_math.cpp)
target_include_directories(amber-tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-tests PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-tests COMMAND amber-tests)

add_executable(amber-test-path test_path.cpp)
target_include_directories(amber-test-path PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-path PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-path COMMAND amber-test-path)

add_executable(amber-test-scrub-resolution test_scrub_resolution.cpp)
target_include_directories(amber-test-scrub-resolution PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-scrub-resolution PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-scrub-resolution COMMAND amber-test-scrub-resolution)

add_executable(amber-test-shadertoy test_shadertoy.cpp)
target_include_directories(amber-test-shadertoy PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(amber-test-shadertoy PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(amber-test-shadertoy PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-shadertoy COMMAND amber-test-shadertoy)

add_executable(amber-test-scopes test_scopes.cpp)
target_include_directories(amber-test-scopes PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-scopes PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-scopes COMMAND amber-test-scopes)

add_executable(amber-test-curves test_curves.cpp)
target_include_directories(amber-test-curves PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-curves PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-curves COMMAND amber-test-curves)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(amber-test-scopewidget test_scopewidget.cpp ${CMAKE_SOURCE_DIR}/ui/scopewidget.cpp)
target_include_directories(amber-test-scopewidget PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-scopewidget PRIVATE amber-engine Qt6::Widgets Qt6::Test)
set_target_properties(amber-test-scopewidget PROPERTIES AUTOMOC ON)
add_test(NAME amber-test-scopewidget COMMAND amber-test-scopewidget)
set_tests_properties(amber-test-scopewidget PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Same pattern: the self-contained ui/curveeditor.cpp (QWidget over core/curves) driven with QTest mouse events
add_executable(amber-test-curveeditor test_curveeditor.cpp ${CMAKE_SOURCE_DIR}/ui/curveeditor.cpp)
target_include_directories(amber-test-curveeditor PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-curveeditor PRIVATE amber-engine Qt6::Widgets Qt6::Test)
set_target_properties(amber-test-curveeditor PROPERTIES AUTOMOC ON)
add_test(NAME amber-test-curveeditor COMMAND amber-test-curveeditor)
set_tests_properties(amber-test-curveeditor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Same pattern: the field widget wrappers Effect Controls uses for multi-selected clips, with the widgets they drive.
# Not effectfieldwidgetfactory.cpp, whose color and text wrappers pull the viewer and the main window.
add_executable(amber-test-fieldwidgets test_fieldwidgets.cpp
  ${CMAKE_SOURCE_DIR}/effects/ui/effectfieldwidget.cpp
  ${CMAKE_SOURCE_DIR}/effects/ui/boolfieldwidget.cpp
  ${CMAKE_SOURCE_DIR}/effects/ui/combofieldwidget.cpp
  ${CMAKE_SOURCE_DIR}/effects/ui/curvefieldwidget.cpp
  ${CMAKE_SOURCE_DIR}/effects/ui/doublefieldwidget.cpp
  ${CMAKE_SOURCE_DIR}/ui/comboboxex.cpp
  ${CMAKE_SOURCE_DIR}/ui/curveeditor.cpp
  ${CMAKE_SOURCE_DIR}/ui/labelslider.cpp
  ${CMAKE_SOURCE_DIR}/ui/menu.cpp
  ${CMAKE_SOURCE_DIR}/ui/styling.cpp
)
target_include_directories(amber-test-fieldwidgets PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-fieldwidgets PRIVATE amber-engine Qt6::Widgets Qt6::Test)
set_target_properties(amber-test-fieldwidgets PROPERTIES AUTOMOC ON)
add_test(NAME amber-test-fieldwidgets COMMAND amber-test-fieldwidgets)
set_tests_properties(amber-test-fieldwidgets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(amber-test-timer-format test_timer_format.cpp)
target_include_directories(amber-test-timer-format PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-timer-format PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-timer-format COMMAND amber-test-timer-format)

add_executable(amber-test-config test_config.cpp)
target_include_directories(amber-test-config PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-config PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-config COMMAND amber-test-config)

add_executable(amber-test-audio test_audio.cpp)
target_include_directories(amber-test-audio PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-audio PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-audio COMMAND amber-test-audio)

add_executable(amber-test-headers test_core_headers.cpp)
target_include_directories(amber-test-headers PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-headers PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-headers COMMAND amber-test-headers)

add_executable(amber-engine-tests test_engine.cpp)
target_include_directories(amber-engine-tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-engine-tests PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-engine-tests COMMAND amber-engine-tests)

add_executable(amber-test-srt test_srt.cpp)
target_include_directories(amber-test-srt PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-srt PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-srt COMMAND amber-test-srt)

add_executable(amber-test-effects test_effects.cpp)
target_include_directories(amber-test-effects PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-effects PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-effects COMMAND amber-test-effects)

add_executable(amber-test-projectio test_projectio.cpp)
target_include_directories(amber-test-projectio PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-projectio PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-projectio COMMAND amber-test-projectio)

add_executable(amber-test-shortcutfile test_shortcutfile.cpp)
target_include_directories(amber-test-shortcutfile PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-shortcutfile PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-shortcutfile COMMAND amber-test-shortcutfile)

add_executable(amber-test-keyframes test_keyframes.cpp)
target_include_directories(amber-test-keyframes PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-keyframes PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-keyframes COMMAND amber-test-keyframes)

add_executable(amber-test-undo-markers test_undo_markers.cpp)
target_include_directories(amber-test-undo-markers PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-undo-markers PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-undo-markers COMMAND amber-test-undo-markers)

add_executable(amber-test-clipboard test_clipboard_serializer.cpp)
target_include_directories(amber-test-clipboard PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-clipboard PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-clipboard COMMAND amber-test-clipboard)

add_executable(amber-test-deletion-plan test_deletion_plan.cpp)
target_include_directories(amber-test-deletion-plan PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-deletion-plan PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-deletion-plan COMMAND amber-test-deletion-plan)

# QRhi headless smoke test
add_executable(amber-test-rhi-smoke test_rhi_smoke.cpp)
target_include_directories(amber-test-rhi-smoke PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-rhi-smoke PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-rhi-smoke COMMAND amber-test-rhi-smoke)

# RhiResourceCache — pipeline cache and per-frame dynamic buffer pool (AMBER_TEST_RHI=vulkan for a local Vulkan run)
add_executable(amber-test-rhi-cache test_rhi_cache.cpp)
target_include_directories(amber-test-rhi-cache PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-rhi-cache PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-rhi-cache COMMAND amber-test-rhi-cache)

# Rendering integration test — exercises QRhi pipeline with real engine types
add_executable(amber-test-rendering test_rendering.cpp)
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

add_executable(amber-test-recordingtap
  test_recordingtap.cpp
  ${CMAKE_SOURCE_DIR}/rendering/recordingtap.cpp
  ${CMAKE_SOURCE_DIR}/rendering/recordingtap.h
)
target_include_directories(amber-test-recordingtap PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-recordingtap PRIVATE Qt6::Test Qt6::Core)
add_test(NAME amber-test-recordingtap COMMAND amber-test-recordingtap)

# Effect integration tests — exercises compose_sequence() with real effects.
add_executable(amber-test-effects-gpu
  test_effects_gpu.cpp
  test_render_harness.cpp
  test_render_harness.h
)
target_include_directories(amber-test-effects-gpu PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(amber-test-effects-gpu PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(amber-test-effects-gpu PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-effects-gpu COMMAND amber-test-effects-gpu)

# Headless project loading — LoadThread on a hand-written .ove, no UI code linked
add_executable(amber-test-loadthread test_loadthread.cpp)
target_include_directories(amber-test-loadthread PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(amber-test-loadthread PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(amber-test-loadthread PRIVATE amber-engine Qt6::Test)
add_test(NAME amber-test-loadthread COMMAND amber-test-loadthread)

# .ove round trip — LoadThread + project/projectsaver, save -> load -> save must be byte-identical
add_executable(amber-test-projectsaver test_projectsaver.cpp)
target_include_directories(amber-test-projectsaver PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(amber-test-projectsaver PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(amber-test-projectsaver PRIVATE amber-engine Qt6::Gui Qt6::Test)
add_test(NAME amber-test-projectsaver COMMAND amber-test-projectsaver)

# Headless export — loads a hand-written .ove and exports it through prepare_export() + ExportThread, then decodes
# the file. Proof that the engine renders without the UI: amber-engine + Qt Gui/Test only, no QtWidgets.
add_executable(amber-test-headless-render test_headless_render.cpp)
target_include_directories(amber-test-headless-render PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(amber-test-headless-render PRIVATE amber-engine Qt6::Gui Qt6::Test)
add_test(NAME amber-test-headless-render COMMAND amber-test-headless-render)

# Render benchmark — run by hand (amber-bench-render --help), deliberately not an add_test()
add_executable(amber-bench-render bench_render.cpp test_render_harness.cpp test_render_harness.h)
target_include_directories(amber-bench-render PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(amber-bench-render PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(amber-bench-render PRIVATE amber-engine Qt6::Test)
