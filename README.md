# Olive EVA-01

Fork of [Olive Video Editor](https://github.com/olive-editor/olive) `0.1.x` — ported to **Qt 6** and **FFmpeg 7/8**.

> **DISCLAIMER: AI-MAINTAINED CODE**
>
> The original Olive 0.1 codebase is hand-written (circa 2019 — well before ChatGPT was a thing). This fork, however, is vibe-coded: I just wanted Olive 0.1 to work on a modern stack and had very little knowledge of the codebase itself. All porting work (Qt 5 to 6, FFmpeg API updates, Wayland fixes, Windows cross-compilation) was done with AI assistance. No new features, no refactoring ambitions — just making it build and run.

![screen](https://www.olivevideoeditor.org/img/screenshot.jpg)

## What changed

- Qt 5 → Qt 6 (including Wayland compositing fix)
- FFmpeg 3.x → FFmpeg 7/8 API (deprecated calls replaced, compat guards for 3.4–8)
- `QLatin1String` wrappers for Qt 6.4 compatibility (Debian/Ubuntu)
- Windows cross-compilation via Fedora mingw64
- Hardware-accelerated video decoding (VAAPI on Linux, D3D11VA on Windows, VideoToolbox on macOS)
- GPU YUV→RGB conversion via OpenGL shader (bypasses CPU format conversion for YUV420P/NV12 frames)
- Frei0r plugin lazy reconstruction (handles dimension changes between project load and playback)
- Graceful audio filter graph failure handling (no more phantom audio on silent tracks)

## Build (Linux)

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

**Dependencies:** Qt 6 (Core, Gui, Widgets, Multimedia, OpenGL, OpenGLWidgets, Svg, LinguistTools), FFmpeg 3.4–8 (avutil, avcodec, avformat, avfilter, swscale, swresample), OpenGL.

## Build (Docker)

```bash
# NSIS installer
docker build -f packaging/windows/cross-compile.dockerfile --target package -t olive-win64 .

# Debian 12 .deb
docker build -f packaging/linux/debian.dockerfile --target package -t olive-debian .

# Ubuntu 24.04 .deb
docker build -f packaging/linux/ubuntu.dockerfile --target deb -t olive-ubuntu-deb .

# AppImage
docker build -f packaging/linux/ubuntu.dockerfile --target appimage -t olive-appimage .
```

## Packages

Pre-built packages available at [v0.1.3-nightly](https://github.com/baptisterajaut/olive/releases/tag/v0.1.3-nightly):

| Platform | File |
|----------|------|
| Windows (installer) | `olive-setup.exe` |
| Linux (AppImage) | `Olive-0.1.3-x86_64.AppImage` |
| Debian 12 | `olive-editor_0.1.3-1_debian12_amd64.deb` |
| Ubuntu 24.04 | `olive-editor_0.1.3-1_ubuntu2404_amd64.deb` |
| Arch Linux | `olive-editor-0.1.3-1-x86_64.pkg.tar.zst` |

Tested on Arch Linux only. Other builds are best-effort.

Build scripts in `packaging/linux/` (Dockerfiles, PKGBUILD) and `packaging/windows/` (cross-compile Dockerfile, NSIS).

## Upstream

Based on [olive-editor/olive](https://github.com/olive-editor/olive) by Matt Konecny and the Olive Team. Licensed under GPLv3.
