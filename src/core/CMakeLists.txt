# Core sources — pure utilities, no engine or UI dependencies.
# Added to the main amber-editor target via OBJECT pattern.

target_sources(${OLIVE_TARGET} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/blur.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/blur.h
  ${CMAKE_CURRENT_SOURCE_DIR}/audio.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/audio.h
  ${CMAKE_CURRENT_SOURCE_DIR}/colorlabel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/colorlabel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/appcontext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/appcontext.h
  ${CMAKE_CURRENT_SOURCE_DIR}/math.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/math.h
  ${CMAKE_CURRENT_SOURCE_DIR}/path.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/path.h
  ${CMAKE_CURRENT_SOURCE_DIR}/displaytype.h
  ${CMAKE_CURRENT_SOURCE_DIR}/style.h
  ${CMAKE_CURRENT_SOURCE_DIR}/selection.h
  ${CMAKE_CURRENT_SOURCE_DIR}/guide.h
  ${CMAKE_CURRENT_SOURCE_DIR}/marker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/keyframe.h
)
