# Amber Video Editor

Fork of [Olive Video Editor](https://github.com/olive-editor/olive) `0.1.x`, ported to **Qt 6** and **FFmpeg 7/8**.

> **DISCLAIMER: AI-MAINTAINED CODE**
>
> The original Olive 0.1 codebase is hand-written (circa 2019, well before ChatGPT was a thing). This fork is vibe-coded: porting, bug fixes, and new features are all done with AI assistance (Claude Code). I don't have the C++ chops to maintain this myself, and no one else was picking it up, so here we are.

![screen](.github/amber.jpg)

## What's different from Olive 0.1

**Ported:**
- Qt 5 → Qt 6, FFmpeg 3.x → 7/8, legacy OpenGL → Qt RHI (Vulkan, Metal, D3D12, OpenGL fallback)
- Wayland compositing fix, Windows cross-compilation via Fedora mingw64

**New features:**
- Hardware-accelerated video decoding (VAAPI, D3D11VA, VideoToolbox) — enabled by default
- GPU YUV→RGB conversion via shader (bypasses CPU swscale for YUV420P/NV12)
- Viewer guides (title-safe, action-safe, custom aspect ratios)
- Oak backports: undo history panel, marker/keyframe/speed dialogs, footage relink, export presets, color labels, configurable auto-recovery

**Bug fixes:**
- First-export audio corruption, race conditions, null pointers, memory leaks, Frei0r init, phantom audio on pause, waveform crash, VU meter thread safety, … (probably squashed enough of them for Claude to be declared an ecosystem menace at this point)

## Minimum requirements

Amber is designed to run on modest hardware — sub-3 MB binary, ~70 MB idle RAM. The goal is to keep video editing accessible on machines that heavier NLEs have left behind.

- **GPU:** OpenGL 3.2 or newer (Intel HD Graphics / Sandy Bridge 2011+, any discrete GPU from the last 15 years). Vulkan, Metal, or D3D12 are used when available but not required.
- **CPU:** any x86-64 processor.
- **RAM:** 512 MB free is enough for simple edits; more helps with long timelines and high-res footage.
- **Linux:** Arch Linux natively (PKGBUILD). Everything else: use the AppImage (it bundles Qt 6.10). No .deb — Debian 13 and Ubuntu 24.04 ship Qt 6.4/6.8 which lack the QRhi private APIs we need. Ubuntu 26.04 will likely work natively.
- **Windows:** Windows 10 or newer (Qt 6 requirement).
- **macOS:** all Apple Silicon machines work. Intel Macs probably too, but untested.

If your GPU is older than OpenGL 3.2 (Intel GMA era), use [version 1.1.0](https://github.com/baptisterajaut/amber/releases/tag/v1.1.0) which uses the legacy OpenGL 2.x renderer. The 1.1.x branch continues to receive bug fixes.

## Roadmap

1.x is feature-complete and in maintenance mode. A massive 2.0 is brewing — GPU-native effects, ShaderToy import, scopes, 3-point editing, rendering pipeline overhaul. See [ROADMAP.md](ROADMAP.md).

## Packages

Pre-built packages for Windows, Linux (AppImage) and macOS are available on the [Releases](https://github.com/baptisterajaut/amber/releases) page. Arch Linux users: build from the PKGBUILD in `packaging/linux/`. Tested on Arch Linux only; other builds are best-effort.

Build scripts in `packaging/linux/` (Dockerfiles, PKGBUILD) and `packaging/windows/` (cross-compile Dockerfile, NSIS).

## Build (Linux)

```bash
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Dependencies:** Qt 6.8+ (Core, Gui, GuiPrivate, Widgets, Multimedia, OpenGL, OpenGLWidgets, ShaderTools, ShaderToolsPrivate, Svg, LinguistTools), FFmpeg 3.4–8 (avutil, avcodec, avformat, avfilter, swscale, swresample).

## Build (Docker)

```bash
# AppImage (Qt 6.10 + PipeWire audio — covers Ubuntu 24.04+)
docker buildx build -f packaging/linux/appimage.dockerfile --output type=local,dest=./out .

# Windows NSIS installer (cross-compiled from Fedora)
docker build -f packaging/windows/cross-compile.dockerfile --target package -t amber-win64 .
docker run --rm amber-win64 cat /out/amber-setup.exe > amber-setup.exe
```

## Build (macOS)

```bash
brew install qt@6 ffmpeg cmake
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix ffmpeg)"
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
cmake --build build -j$(sysctl -n hw.ncpu)
```

To create an app bundle: `macdeployqt build/Amber.app`

## Build (Arch Linux)

```bash
cd packaging/linux
makepkg -si
```

## Upstream

Based on [olive-editor/olive](https://github.com/olive-editor/olive) by [MattKC](https://github.com/itsmattkc) and the Olive Team. Licensed under GPLv3.
