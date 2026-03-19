# Engine sources — clip/cacher/sequence model + undo system.
# Added to the main amber-editor target via OBJECT pattern.

target_sources(${OLIVE_TARGET} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/cacher.h
  ${CMAKE_CURRENT_SOURCE_DIR}/cacher.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cacher_video.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cacher_audio.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/clip.h
  ${CMAKE_CURRENT_SOURCE_DIR}/clip.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sequence.h
  ${CMAKE_CURRENT_SOURCE_DIR}/sequence.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/comboaction.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/comboaction.h
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undoactions.h
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undostack.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undostack.h
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_clip.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_clip.h
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_effect.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_effect.h
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_generic.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_generic.h
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_guide.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_guide.h
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_media.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_media.h
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_timeline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo_timeline.h
  ${CMAKE_CURRENT_SOURCE_DIR}/undo/undo.h
)
