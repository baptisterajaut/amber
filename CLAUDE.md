# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Olive Video Editor — free, open-source non-linear video editor (GPLv3). C++11, Qt 6, FFmpeg, OpenGL. This is the legacy `0.1.x` branch (ported to FFmpeg 7/8 API and Qt 6).

## Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

**Dependencies:** Qt 6 (Core, Gui, Widgets, Multimedia, OpenGL, OpenGLWidgets, Svg, LinguistTools), FFmpeg 3.4–8 (avutil, avcodec, avformat, avfilter, swscale, swresample), OpenGL. Optional: Frei0r plugins (disable with `-DNOFREI0R`), OpenColorIO (Windows only).

No test suite exists in this branch.

## Packaging

All packages at https://github.com/baptisterajaut/olive/releases/tag/v0.1.3-nightly

Docker multi-stage builds (build stage compiles, package stage produces artifact):
- `packaging/linux/debian.dockerfile` — Debian 12 `.deb` (`--target package`)
- `packaging/linux/ubuntu.dockerfile` — Ubuntu 24.04 `.deb` (`--target deb`) + AppImage (`--target appimage`), shared build stage
- `packaging/windows/cross-compile.dockerfile` — Windows NSIS installer via Fedora mingw64 (`--target package`)
- `packaging/linux/PKGBUILD` — Arch, run with `makepkg` natively

## Platform support

Tested on Arch Linux only. Debian, Ubuntu, AppImage and Windows builds are best-effort.

## Known issues

- **FFmpeg compat**: `#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 0, 0)` guards needed for `avcodec_get_supported_config()` (replaces `codec->pix_fmts`/`codec->sample_fmts`). `#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 29, 100)` for `AV_FRAME_FLAG_INTERLACED`.
- **Qt 6.4 compat**: `QStringView == "literal"` doesn't compile on Qt 6.4 (Debian/Ubuntu). All `stream.name()`, `attr.name()`, `attr.value()`, `reader.name()` comparisons must use `QLatin1String()` wrapper.

## Code style

clang-format with `.clang-format` in repo root (Google-based, 2-space indent, 120-col limit, attached braces, left pointer alignment). Run `clang-format -i <file>` to format.

## Architecture

**Entry point:** `main.cpp` → `OliveGlobal` singleton (`global/global.h`) bootstraps the app, manages project lifecycle and auto-recovery.

**Core layers:**

- **Timeline model** (`timeline/`): `Sequence` (timeline with tracks), `Clip` (individual clip on a track), `Marker`, `Selection`. These are pure data structures.
- **Project model** (`project/`): `Media` items in a `ProjectModel` (Qt Model/View). `Footage` holds decoded media metadata. `LoadThread` handles `.ov` project file parsing. `PreviewGenerator`/`ProxyGenerator` create thumbnails and proxy files.
- **Rendering** (`rendering/`): `RenderThread` runs OpenGL in a dedicated thread with FBOs. `ExportThread` encodes final output via FFmpeg. `Cacher` decodes and caches frames per-clip. Audio mixing in `audio.cpp`.
- **Effects** (`effects/`): `Effect` base class with `EffectRow`/`EffectField` for parameters and keyframing. Shader-based effects defined as XML+GLSL pairs in `effects/shaders/`. Built-in effects (transform, text, transitions) in `effects/internal/`. Frei0r plugin bridge in `frei0reffect.cpp`.
- **UI panels** (`panels/`): `Project` (media browser), `Timeline`, `Viewer`, `EffectControls`, `GraphEditor`. Each panel is a dockable widget.
- **UI widgets** (`ui/`): `MainWindow` orchestrates layout. `TimelineWidget` handles track rendering/interaction. `ViewerWidget` does OpenGL playback. Custom widgets: `LabelSlider`, `KeyframeView`, `CollapsibleWidget`, etc.
- **Undo** (`undo/`): Command pattern via `ComboAction` (groups multiple undo steps). All edits go through `UndoStack`.
- **Dialogs** (`dialogs/`): Export, preferences, new sequence, media properties, etc.

**Effect system:** Shader effects are discovered at runtime from `effects/shaders/*.xml`. Each XML declares parameters (fields) and references a `.frag` shader. The `effectloaders.cpp` file handles discovery and registration.

**Project file format:** Custom XML-based `.ov` format. Save version `190219` (YYMMDD). Loading handled by `LoadThread`, saving by `OliveGlobal::save_project()`.

**Translations:** Qt Linguist `.ts` files in `ts/`, compiled to `.qm` at build time. 15 languages supported.
