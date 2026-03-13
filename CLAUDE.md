# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Amber Video Editor — fork of Olive 0.1.x, free open-source non-linear video editor (GPLv3). C++11, Qt 6, FFmpeg, OpenGL. Targets performance on modest hardware (sub-3 MB binary, ~70 MB idle RAM).

## Roadmap

See [ROADMAP.md](ROADMAP.md). 1.x: RHI port, Oak backports, warnings cleanup (order TBD). 2.0: GPU-native effects, new editing features, .amb format. 1.x stays supported after 2.0.

## Build

```bash
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Dependencies:** Qt 6 (Core, Gui, Widgets, Multimedia, OpenGL, OpenGLWidgets, Svg, LinguistTools), FFmpeg 3.4–8 (avutil, avcodec, avformat, avfilter, swscale, swresample), OpenGL. Optional: Frei0r plugins (disable with `-DNOFREI0R`), OpenColorIO (Windows only).

No test suite exists in this branch.

## Packaging

All packages at https://github.com/baptisterajaut/amber/releases

Docker builds (build stage compiles, package stage produces artifact via `FROM scratch` + `--output`):
- `packaging/linux/debian.dockerfile` — Debian 12 `.deb` (`--target package`)
- `packaging/linux/ubuntu.dockerfile` — Ubuntu 24.04 `.deb` (system Qt 6.4, single-stage)
- `packaging/linux/appimage.dockerfile` — AppImage (Qt 6.10 via aqtinstall, native PipeWire audio)
- `packaging/linux/dev.dockerfile` — dev iterative AppImage build (volume mounts, cmake cache persistence)
- `packaging/windows/cross-compile.dockerfile` — Windows NSIS installer via Fedora mingw64 (`--target package`)
- `packaging/linux/PKGBUILD` — Arch, run with `makepkg` natively

## Platform support

Tested on Arch Linux only. Debian, Ubuntu, AppImage and Windows builds are best-effort.

## Known issues

- **FFmpeg compat**: `#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 0, 0)` guards needed for `avcodec_get_supported_config()` (replaces `codec->pix_fmts`/`codec->sample_fmts`). `#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 29, 100)` for `AV_FRAME_FLAG_INTERLACED`.
- **Qt 6.4 compat**: `QStringView == "literal"` doesn't compile on Qt 6.4 (Debian/Ubuntu). All `stream.name()`, `attr.name()`, `attr.value()`, `reader.name()` comparisons must use `QLatin1String()` wrapper.
- **Qt 6.10 PipeWire audio backend**: Does not enumerate Bluetooth audio sinks. Workaround: `qputenv("QT_AUDIO_BACKEND", "pulseaudio")` in `main.cpp` forces PulseAudio engine (works through `pipewire-pulse`). Safe no-op on Windows/macOS.
- **AppImage VAAPI**: `libva` must NOT be bundled in the AppImage (`--exclude-library "libva*"`). The host's libva loads its own GPU-specific drivers; bundling the container's libva breaks driver discovery.
- **AppImage FFmpeg ABI**: Qt 6.10 bundles FFmpeg 7.x. `FindFFMPEG.cmake` uses `NO_CMAKE_PATH` to prevent cmake from finding Qt's FFmpeg instead of the system one. Removing Qt's `libffmpegmediaplugin.so` before linuxdeploy avoids bundling a second FFmpeg.
- **Cacher reconfigure**: Adding/removing an ImageFlag effect (e.g. Frei0r) on a clip requires closing and reopening the clip so the cacher switches between YUV and RGBA pipeline. This is handled by `NeedsCacherReconfigure()` in `compose_sequence()`.
- **Frei0r dimensions**: Frei0r instances are constructed with fixed dimensions. If `media_width()`/`media_height()` aren't available yet (project load before media analysis), `process_image()` lazily reconstructs the instance when dimensions change.

## Code style

clang-format with `.clang-format` in repo root (Google-based, 2-space indent, 120-col limit, attached braces, left pointer alignment). Run `clang-format -i <file>` to format.

## Architecture

**Entry point:** `main.cpp` → `OliveGlobal` singleton (`global/global.h`) bootstraps the app, manages project lifecycle and auto-recovery.

**Core layers:**

- **Timeline model** (`timeline/`): `Sequence` (timeline with tracks), `Clip` (individual clip on a track), `Marker`, `Selection`. These are pure data structures.
- **Project model** (`project/`): `Media` items in a `ProjectModel` (Qt Model/View). `Footage` holds decoded media metadata. `LoadThread` handles `.ov` project file parsing. `PreviewGenerator`/`ProxyGenerator` create thumbnails and proxy files.
- **Rendering** (`rendering/`): `RenderThread` runs OpenGL in a dedicated thread with FBOs. `ExportThread` encodes final output via FFmpeg. `Cacher` decodes and caches frames per-clip. Audio mixing in `audio.cpp`. GPU YUV→RGB conversion in `yuv2rgb.frag` shader (uploaded via `Clip::Retrieve()`). `QuadBuffer` provides stateless quad drawing (transient VAO+VBO per draw, thread-safe). `MatrixUtil` provides CPU-side ortho/identity matrices replacing the old GL matrix stack.
- **Effects** (`effects/`): `Effect` base class with `EffectRow`/`EffectField` for parameters and keyframing. Shader-based effects defined as XML+GLSL pairs in `effects/shaders/`. Built-in effects (transform, text, transitions) in `effects/internal/`. Frei0r plugin bridge in `frei0reffect.cpp`.
- **UI panels** (`panels/`): `Project` (media browser), `Timeline`, `Viewer`, `EffectControls`, `GraphEditor`. Each panel is a dockable widget.
- **UI widgets** (`ui/`): `MainWindow` orchestrates layout. `TimelineWidget` handles track rendering/interaction. `ViewerWidget` does OpenGL playback. Custom widgets: `LabelSlider`, `KeyframeView`, `CollapsibleWidget`, etc.
- **Undo** (`undo/`): Command pattern via `ComboAction` (groups multiple undo steps). All edits go through `UndoStack`.
- **Dialogs** (`dialogs/`): Export, preferences, new sequence, media properties, etc.

**Effect system:** Shader effects are discovered at runtime from `effects/shaders/*.xml`. Each XML declares parameters (fields) and references a `.frag` shader. The `effectloaders.cpp` file handles discovery and registration.

**Project file format:** Custom XML-based `.ov` format (backward compatible with Olive 0.1). Save version `190219` (YYMMDD). Loading handled by `LoadThread`, saving by `OliveGlobal::save_project()`.

**Video decode pipeline:** `Cacher` (background thread) decodes frames via FFmpeg. Two paths based on `Clip::NeedsCpuRgba()`:
- **GPU path** (default): AVFilter outputs YUV420P or NV12 → `Clip::Retrieve()` uploads Y/U/V planes to GL textures → `yuv2rgb.frag` shader converts to RGBA in an FBO → `cached_texture_id` points to FBO texture.
- **CPU path** (when clip has ImageFlag effects like Frei0r): AVFilter outputs RGBA → effects process CPU buffer → uploaded to `QOpenGLTexture` → `cached_texture_id` points to that texture.

Hardware-accelerated decoding (VAAPI/D3D11VA/VideoToolbox) is optional (preference toggle). Hwaccel frames are transferred to system memory before entering the pipeline above.

**GL pipeline (Phase 0 modernized):** No fixed-function GL calls remain (`glBegin`/`glEnd`, `glMatrixMode`, `glOrtho`, `glPushMatrix`, etc. all removed). Rendering uses explicit VBOs via `QuadBuffer::draw()`, CPU-side `QMatrix4x4` uniforms via `MatrixUtil`, and shader programs with `uniform mat4 mvp_matrix` + `attribute vec2 a_position/a_texcoord`. Effects accumulate transforms in `GLTextureCoords.transform` (replaces GL matrix stack). Viewer overlays (title-safe, guides, gizmos) use QPainter. Still on Compatibility Profile — 35+ effect shaders use `#version 110` (`attribute`/`varying`/`gl_FragColor`); Core Profile migration requires updating them all to `#version 150`.

**Translations:** Qt Linguist `.ts` files in `ts/`, compiled to `.qm` at build time. 15 languages supported.
