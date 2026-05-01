cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(amber-editor LANGUAGES CXX)

set(AMBER_VERSION "dev" CACHE STRING "Application version (pass -DAMBER_VERSION=x.y.z)")

option(BUILD_DOXYGEN "Build Doxygen documentation" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(AMBER_DEFINITIONS -DQT_DEPRECATED_WARNINGS)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(UNIX AND NOT APPLE AND NOT DEFINED OpenGL_GL_PREFERENCE)
  set(OpenGL_GL_PREFERENCE LEGACY)
endif()
find_package(OpenGL REQUIRED)

if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
endif()

find_package(Qt6 REQUIRED
  COMPONENTS
  Core
  Gui
  GuiPrivate
  Widgets
  Multimedia
  OpenGL
  OpenGLWidgets
  ShaderTools
  ShaderToolsPrivate
  Svg
  LinguistTools
)

find_package(FFMPEG 3.4 REQUIRED
  COMPONENTS
  avutil
  avcodec
  avformat
  avfilter
  swscale
  swresample
)

option(NOFREI0R "Disable Frei0r plugin support" OFF)
if(NOFREI0R)
  list(APPEND AMBER_DEFINITIONS -DNOFREI0R)
endif()

if(WIN32)
  find_package(OpenColorIO)
  if(OPENCOLORIO_FOUND)
    list(APPEND AMBER_DEFINITIONS -DAMBER_OCIO)
  endif()
endif()

if(NOT DEFINED GIT_HASH)
  if(EXISTS "${CMAKE_SOURCE_DIR}/../.git")
    find_package(Git)
    if(GIT_FOUND)
      execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%h
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
    endif()
  elseif(UNIX AND NOT APPLE)
    # Fallback for Ubuntu/Launchpad (extracts Git hash from debian/changelog rather than Git repo)
    # (see https://answers.launchpad.net/launchpad/+question/678556)
    execute_process(COMMAND sh -c "grep -Po '(?<=-)(([a-z0-9])\\w+)(?=\\+)' -m 1 changelog"
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/../packaging/debian
      OUTPUT_VARIABLE GIT_HASH
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  endif()
endif()
list(APPEND AMBER_DEFINITIONS -DAPPVERSION="${AMBER_VERSION}")
if(DEFINED GIT_HASH)
  message("Amber: git hash = " "${GIT_HASH}")
  list(APPEND AMBER_DEFINITIONS -DGITHASH="${GIT_HASH}")
else()
  message("Amber: No git hash defined!")
endif()

find_package(Doxygen)

add_library(amber-engine OBJECT
  # effects/ (not effects/ui/)
  effects/effect.cpp effects/effect.h
  effects/effectfield.cpp effects/effectfield.h
  effects/effectfields.h
  effects/effectgizmo.cpp effects/effectgizmo.h
  effects/effectloaders.cpp effects/effectloaders.h
  effects/effectrow.cpp effects/effectrow.h
  effects/keyframe.cpp effects/keyframe.h
  effects/transition.cpp effects/transition.h
  # effects/fields/
  effects/fields/boolfield.cpp effects/fields/boolfield.h
  effects/fields/buttonfield.cpp effects/fields/buttonfield.h
  effects/fields/colorfield.cpp effects/fields/colorfield.h
  effects/fields/combofield.cpp effects/fields/combofield.h
  effects/fields/doublefield.cpp effects/fields/doublefield.h
  effects/fields/filefield.cpp effects/fields/filefield.h
  effects/fields/fontfield.cpp effects/fields/fontfield.h
  effects/fields/labelfield.cpp effects/fields/labelfield.h
  effects/fields/stringfield.cpp effects/fields/stringfield.h
  # effects/internal/
  effects/internal/audionoiseeffect.cpp effects/internal/audionoiseeffect.h
  effects/internal/cornerpineffect.cpp effects/internal/cornerpineffect.h
  effects/internal/crossdissolvetransition.cpp effects/internal/crossdissolvetransition.h
  effects/internal/cubetransition.h
  effects/internal/dropshadoweffect.cpp effects/internal/dropshadoweffect.h
  effects/internal/exponentialfadetransition.cpp effects/internal/exponentialfadetransition.h
  effects/internal/fillleftrighteffect.cpp effects/internal/fillleftrighteffect.h
  effects/internal/frei0reffect.cpp effects/internal/frei0reffect.h
  effects/internal/gradienteffect.cpp effects/internal/gradienteffect.h
  effects/internal/linearfadetransition.cpp effects/internal/linearfadetransition.h
  effects/internal/logarithmicfadetransition.cpp effects/internal/logarithmicfadetransition.h
  effects/internal/paneffect.cpp effects/internal/paneffect.h
  effects/internal/richtexteffect.cpp effects/internal/richtexteffect.h
  effects/internal/shakeeffect.cpp effects/internal/shakeeffect.h
  effects/internal/solideffect.cpp effects/internal/solideffect.h
  effects/internal/srtparser.cpp effects/internal/srtparser.h
  effects/internal/subtitleeffect.cpp effects/internal/subtitleeffect.h
  effects/internal/texteffect.cpp effects/internal/texteffect.h
  effects/internal/timecodeeffect.cpp effects/internal/timecodeeffect.h
  effects/internal/toneeffect.cpp effects/internal/toneeffect.h
  effects/internal/transformeffect.cpp effects/internal/transformeffect.h
  effects/internal/voideffect.cpp effects/internal/voideffect.h
  effects/internal/volumeeffect.cpp effects/internal/volumeeffect.h
  effects/internal/vsthost.cpp effects/internal/vsthost.h
  effects/internal/internalshaders.qrc
  # rendering/ (not audio_ui)
  rendering/audio.cpp rendering/audio.h
  rendering/clipqueue.cpp rendering/clipqueue.h
  rendering/exportthread.cpp rendering/exportthread.h
  rendering/recordingtap.cpp rendering/recordingtap.h
  rendering/renderfunctions.cpp rendering/renderfunctions.h
  rendering/renderthread.cpp rendering/renderthread.h
  # project/ (data model only)
  project/clipboard.cpp project/clipboard.h
  project/footage.cpp project/footage.h
  project/loadthread.cpp project/loadthread.h
  project/media.cpp project/media.h
  project/projectelements.h
  project/projectmodel.cpp project/projectmodel.h
  # global/ (config/projectio/debug)
  global/config.cpp global/config.h
  global/debug.cpp global/debug.h
  global/projectio.cpp global/projectio.h
  # other
  include/vestige.h
  timeline/mediaimportdata.cpp timeline/mediaimportdata.h
)

add_subdirectory(core)
add_subdirectory(engine)

target_include_directories(amber-engine PUBLIC ${CMAKE_SOURCE_DIR})
set_target_properties(amber-engine PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_definitions(amber-engine PUBLIC ${AMBER_DEFINITIONS})

target_link_libraries(amber-engine PUBLIC
  OpenGL::GL
  Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Widgets Qt6::Multimedia
  Qt6::OpenGL Qt6::OpenGLWidgets Qt6::ShaderToolsPrivate Qt6::Svg
  FFMPEG::avutil FFMPEG::avcodec FFMPEG::avformat FFMPEG::avfilter
  FFMPEG::swscale FFMPEG::swresample
  $<$<AND:$<PLATFORM_ID:Linux>,$<BOOL:${X11_FOUND}>>:${X11_LIBRARIES}>
)

set(UI_SOURCES
  dialogs/aboutdialog.cpp dialogs/aboutdialog.h
  dialogs/actionsearch.cpp dialogs/actionsearch.h
  dialogs/advancedvideodialog.cpp dialogs/advancedvideodialog.h
  dialogs/autocutsilencedialog.cpp dialogs/autocutsilencedialog.h
  dialogs/clippropertiesdialog.cpp dialogs/clippropertiesdialog.h
  dialogs/debugdialog.cpp dialogs/debugdialog.h
  dialogs/demonotice.cpp dialogs/demonotice.h
  dialogs/exportdialog.cpp dialogs/exportdialog.h
  dialogs/exportsavepresetdialog.cpp dialogs/exportsavepresetdialog.h
  dialogs/footagerelinkdialog.cpp dialogs/footagerelinkdialog.h
  dialogs/keyframepropertiesdialog.cpp dialogs/keyframepropertiesdialog.h
  dialogs/loaddialog.cpp dialogs/loaddialog.h
  dialogs/markerpropertiesdialog.cpp dialogs/markerpropertiesdialog.h
  dialogs/mediapropertiesdialog.cpp dialogs/mediapropertiesdialog.h
  dialogs/newsequencedialog.cpp dialogs/newsequencedialog.h
  dialogs/preferencesdialog.cpp dialogs/preferencesdialog.h
  dialogs/proxydialog.cpp dialogs/proxydialog.h
  dialogs/replaceclipmediadialog.cpp dialogs/replaceclipmediadialog.h
  dialogs/speeddialog.cpp dialogs/speeddialog.h
  dialogs/texteditdialog.cpp dialogs/texteditdialog.h
  effects/ui/boolfieldwidget.cpp effects/ui/boolfieldwidget.h
  effects/ui/buttonfieldwidget.cpp effects/ui/buttonfieldwidget.h
  effects/ui/colorfieldwidget.cpp effects/ui/colorfieldwidget.h
  effects/ui/combofieldwidget.cpp effects/ui/combofieldwidget.h
  effects/ui/doublefieldwidget.cpp effects/ui/doublefieldwidget.h
  effects/ui/effectfieldwidget.cpp effects/ui/effectfieldwidget.h
  effects/ui/filefieldwidget.cpp effects/ui/filefieldwidget.h
  effects/ui/fontfieldwidget.cpp effects/ui/fontfieldwidget.h
  effects/ui/labelfieldwidget.cpp effects/ui/labelfieldwidget.h
  effects/ui/stringfieldwidget.cpp effects/ui/stringfieldwidget.h
  global/global.cpp global/global.h
  panels/effectcontrols.cpp panels/effectcontrols.h
  panels/grapheditor.cpp panels/grapheditor.h
  panels/panels.cpp panels/panels.h
  panels/project.cpp panels/project.h
  panels/undohistorypanel.cpp panels/undohistorypanel.h
  panels/timeline.cpp panels/timeline_clipboard.cpp panels/timeline_layout.cpp panels/timeline_layout.h panels/timeline_splitting.cpp panels/timeline_ui.cpp panels/timeline.h
  panels/viewer.cpp panels/viewer.h
  project/previewgenerator.cpp project/previewgenerator.h
  project/projectfilter.cpp project/projectfilter.h
  project/proxygenerator.cpp project/proxygenerator.h
  project/sourcescommon.cpp project/sourcescommon.h
  rendering/audio_ui.cpp rendering/audio_ui.h
  timeline/marker.cpp timeline/marker.h
  ui/appcontextimpl.cpp ui/appcontextimpl.h
  ui/audiomonitor.cpp ui/audiomonitor.h
  ui/blur.cpp ui/blur.h
  ui/checkbox_command.cpp ui/checkbox_command.h
  ui/clickablelabel.cpp ui/clickablelabel.h
  ui/collapsiblewidget.cpp ui/collapsiblewidget.h
  ui/colorlabel.cpp ui/colorlabel.h
  ui/columnedgridlayout.cpp ui/columnedgridlayout.h
  ui/colorbutton.cpp ui/colorbutton.h
  ui/comboboxex.cpp ui/comboboxex.h
  ui/cursors.cpp ui/cursors.h
  ui/effectui.cpp ui/effectui.h
  ui/embeddedfilechooser.cpp ui/embeddedfilechooser.h
  ui/flowlayout.cpp ui/flowlayout.h
  ui/focusfilter.cpp ui/focusfilter.h
  ui/fontcombobox.cpp ui/fontcombobox.h
  ui/graphview.cpp ui/graphview.h
  ui/icons.cpp ui/icons.h
  ui/keyframedrawing.cpp ui/keyframedrawing.h
  ui/keyframenavigator.cpp ui/keyframenavigator.h
  ui/keyframeview.cpp ui/keyframeview.h
  ui/labelslider.cpp ui/labelslider.h
  ui/mainwindow.cpp ui/mainwindow.h
  ui/mediaiconservice.cpp ui/mediaiconservice.h
  ui/menu.cpp ui/menu.h
  ui/menuhelper.cpp ui/menuhelper.h
  ui/panel.cpp ui/panel.h
  ui/playbutton.cpp ui/playbutton.h
  ui/rectangleselect.cpp ui/rectangleselect.h
  ui/rulerwidget.cpp ui/rulerwidget.h
  ui/resizablescrollbar.cpp ui/resizablescrollbar.h
  ui/scrollarea.cpp ui/scrollarea.h
  ui/sourceiconview.cpp ui/sourceiconview.h
  ui/sourcetable.cpp ui/sourcetable.h
  ui/styling.cpp ui/styling.h
  ui/texteditex.cpp ui/texteditex.h
  ui/timelineheader.cpp ui/timelineheader.h
  ui/timelinetools.h
  ui/timelinewidget.cpp ui/timelinewidget_ghosts.cpp ui/timelinewidget_menu.cpp ui/timelinewidget_paint.cpp ui/timelinewidget.h
  ui/viewercontainer.cpp ui/viewercontainer.h
  ui/viewerwidget.cpp ui/viewerwidget.h
  ui/viewerwindow.cpp ui/viewerwindow.h
  main.cpp
)

set(AMBER_RESOURCES
  cursors/cursors.qrc
  icons/icons.qrc
)

set(AMBER_EFFECTS
  effects/shaders/boxblur.frag
  effects/shaders/boxblur.xml
  effects/shaders/bulge.frag
  effects/shaders/bulge.xml
  effects/shaders/chromakey.frag
  effects/shaders/chromakey.xml
  effects/shaders/chromaticaberration.frag
  effects/shaders/chromaticaberration.xml
  effects/shaders/colorcorrection.frag
  effects/shaders/colorcorrection.xml
  effects/shaders/colorsel.frag
  effects/shaders/colorsel.xml
  effects/shaders/common.frag
  effects/shaders/common.vert
  effects/shaders/crop.frag
  effects/shaders/crop.xml
  effects/shaders/crossstitch.frag
  effects/shaders/crossstitch.xml
  effects/shaders/directionalblur.frag
  effects/shaders/directionalblur.xml
  effects/shaders/dropshadow.xml.disabled
  effects/shaders/emboss.frag
  effects/shaders/emboss.xml
  effects/shaders/findedges.frag
  effects/shaders/findedges.xml.disabled
  effects/shaders/fisheye.frag
  effects/shaders/fisheye.xml
  effects/shaders/flip.frag
  effects/shaders/flip.xml
  effects/shaders/gaussianblur.frag
  effects/shaders/gaussianblur.xml
  effects/shaders/huesatbri.frag
  effects/shaders/huesatbri.xml
  effects/shaders/invert.frag
  effects/shaders/invert.xml
  effects/shaders/lumakey.frag
  effects/shaders/lumakey.xml
  effects/shaders/noise.frag
  effects/shaders/noise.xml
  effects/shaders/pixelate.frag
  effects/shaders/pixelate.xml
  effects/shaders/posterize.frag
  effects/shaders/posterize.xml
  effects/shaders/radialblur.frag
  effects/shaders/radialblur.xml
  effects/shaders/ripple.frag
  effects/shaders/ripple.xml
  effects/shaders/sphere.frag
  effects/shaders/sphere.xml
  effects/shaders/swirl.frag
  effects/shaders/swirl.xml
  effects/shaders/tile.frag
  effects/shaders/tile.xml
  effects/shaders/toonify.frag
  effects/shaders/toonify.xml
  effects/shaders/turbulentdisplacement.frag
  effects/shaders/turbulentdisplacement.xml
  effects/shaders/vignette.frag
  effects/shaders/vignette.xml
  effects/shaders/volumetriclight.frag
  effects/shaders/volumetriclight.xml
  effects/shaders/wave.frag
  effects/shaders/wave.xml
)

qt_add_translation(AMBER_QM_FILES
  ts/amber_ar.ts
  ts/amber_bs.ts
  ts/amber_cs.ts
  ts/amber_de.ts
  ts/amber_es.ts
  ts/amber_fr.ts
  ts/amber_it.ts
  ts/amber_ru.ts
  ts/amber_sr.ts
  ts/amber_id.ts
  ts/amber_tr.ts
  ts/amber_pt-BR.ts
  ts/amber_uk.ts
  ts/amber_zh-CN.ts
  ts/amber_zh-TW.ts
)

set(AMBER_TARGET "amber-editor")
if(APPLE)
  set(AMBER_TARGET "Amber")
endif()

add_executable(${AMBER_TARGET}
  ${UI_SOURCES}
  ${AMBER_RESOURCES}
  ${AMBER_EFFECTS}
  ${AMBER_QM_FILES}
)

target_link_libraries(${AMBER_TARGET} PRIVATE
  amber-engine
  $<$<AND:$<PLATFORM_ID:Linux>,$<BOOL:${X11_FOUND}>>:${X11_LIBRARIES}>
)

if(WIN32 AND OPENCOLORIO_FOUND)
  target_link_libraries(${AMBER_TARGET} PRIVATE OpenColorIO)
endif()

# Link-time optimization for Release builds (recovers overhead from function decomposition)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
  set_property(TARGET ${AMBER_TARGET} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# Test suite (only when not cross-compiling)
if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_subdirectory(tests)
endif()

qt_add_shaders(amber-engine "amber_shaders"
  PREFIX "/shaders"
  GLSL "150"
  BASE "rendering/shaders"
  FILES
    rendering/shaders/common.vert
    rendering/shaders/passthrough.frag
    rendering/shaders/yuv2rgb.frag
    rendering/shaders/blending.frag
    rendering/shaders/premultiply.frag
)

# Copy shader effects next to the binary so dev builds find them
add_custom_command(TARGET ${AMBER_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${AMBER_TARGET}>/effects"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AMBER_EFFECTS} "$<TARGET_FILE_DIR:${AMBER_TARGET}>/effects"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Copying shader effects to build directory"
)

file(GLOB AMBER_EXPORT_PRESETS "${CMAKE_CURRENT_SOURCE_DIR}/exportpresets/*")
add_custom_command(TARGET ${AMBER_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${AMBER_TARGET}>/exportpresets"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AMBER_EXPORT_PRESETS} "$<TARGET_FILE_DIR:${AMBER_TARGET}>/exportpresets"
  COMMENT "Copying export presets to build directory"
)

if(APPLE)
  target_compile_definitions(${AMBER_TARGET} PRIVATE GL_SILENCE_DEPRECATION)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(DOXYGEN_FOUND AND BUILD_DOXYGEN)
  set(DOXYGEN_PROJECT_NAME "Amber")
  set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs")
  set(DOXYGEN_EXTRACT_ALL "YES")
  set(DOXYGEN_EXTRACT_PRIVATE "YES")
  doxygen_add_docs(docs ALL ${UI_SOURCES})
endif()

if(UNIX AND NOT APPLE)
  install(TARGETS ${AMBER_TARGET} RUNTIME DESTINATION bin)
  install(FILES ${AMBER_EFFECTS} DESTINATION share/amber-editor/effects)
  install(FILES ${AMBER_EXPORT_PRESETS} DESTINATION share/amber-editor/exportpresets)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE DESTINATION share/amber-editor)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/org.ambervideoeditor.Amber.desktop DESTINATION share/applications)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/org.ambervideoeditor.Amber.appdata.xml DESTINATION share/metainfo)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/org.ambervideoeditor.Amber.xml DESTINATION share/mime/packages)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/16x16/org.ambervideoeditor.Amber.png DESTINATION share/icons/hicolor/16x16/apps)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/32x32/org.ambervideoeditor.Amber.png DESTINATION share/icons/hicolor/32x32/apps)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/48x48/org.ambervideoeditor.Amber.png DESTINATION share/icons/hicolor/48x48/apps)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/64x64/org.ambervideoeditor.Amber.png DESTINATION share/icons/hicolor/64x64/apps)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/128x128/org.ambervideoeditor.Amber.png DESTINATION share/icons/hicolor/128x128/apps)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/256x256/org.ambervideoeditor.Amber.png DESTINATION share/icons/hicolor/256x256/apps)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/512x512/org.ambervideoeditor.Amber.png DESTINATION share/icons/hicolor/512x512/apps)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/16x16/application-vnd.olive-project.png DESTINATION share/icons/hicolor/16x16/mimetypes)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/32x32/application-vnd.olive-project.png DESTINATION share/icons/hicolor/32x32/mimetypes)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/48x48/application-vnd.olive-project.png DESTINATION share/icons/hicolor/48x48/mimetypes)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/64x64/application-vnd.olive-project.png DESTINATION share/icons/hicolor/64x64/mimetypes)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/128x128/application-vnd.olive-project.png DESTINATION share/icons/hicolor/128x128/mimetypes)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/256x256/application-vnd.olive-project.png DESTINATION share/icons/hicolor/256x256/mimetypes)
  install(FILES ${CMAKE_SOURCE_DIR}/../packaging/linux/icons/512x512/application-vnd.olive-project.png DESTINATION share/icons/hicolor/512x512/mimetypes)
  install(FILES ${AMBER_QM_FILES} DESTINATION share/amber-editor/ts)
endif()
