# Olive EVA-01

Fork of [Olive Video Editor](https://github.com/olive-editor/olive) `0.1.x` — ported to **Qt 6** and **FFmpeg 7/8**.

> **DISCLAIMER: AI-MAINTAINED CODE**
>
> The original Olive 0.1 codebase is hand-written (circa 2019 — well before ChatGPT was a thing). This fork is vibe-coded: porting, bug fixes, and new features are all done with AI assistance (Claude Code).

![screen](https://www.olivevideoeditor.org/img/screenshot.jpg)

## What changed

- Qt 5 → Qt 6 (including Wayland compositing fix)
- FFmpeg 3.x → FFmpeg 7/8 API (deprecated calls replaced, compat guards for 3.4–8)
- `QLatin1String` wrappers for Qt 6.4 compatibility (Debian/Ubuntu)
- Windows cross-compilation via Fedora mingw64
- Hardware-accelerated video decoding (VAAPI on Linux, D3D11VA on Windows, VideoToolbox on macOS)
- GPU YUV→RGB conversion via OpenGL shader (bypasses CPU format conversion for YUV420P/NV12 frames)
- Various bug fixes (first-export audio corruption, race conditions, null pointers, memory leaks, Frei0r init, phantom audio on silent tracks, …)

## Build (Linux)

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

**Dependencies:** Qt 6 (Core, Gui, Widgets, Multimedia, OpenGL, OpenGLWidgets, Svg, LinguistTools), FFmpeg 3.4–8 (avutil, avcodec, avformat, avfilter, swscale, swresample), OpenGL.

## Build (Docker)

```bash
# Ubuntu 24.04 .deb + AppImage
docker buildx build -f packaging/linux/ubuntu.dockerfile --target both --output type=local,dest=./out .

# Debian 12 .deb
docker buildx build -f packaging/linux/debian.dockerfile --target package --output type=local,dest=./out .

# Windows NSIS installer (cross-compiled from Fedora)
docker build -f packaging/windows/cross-compile.dockerfile --target package -t olive-win64 .
docker run --rm olive-win64 cat /out/olive-setup.exe > olive-setup.exe
```

## Build (macOS)

```bash
brew install qt@6 ffmpeg cmake
export PATH="$(brew --prefix qt@6)/bin:$PATH"
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix ffmpeg)"
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" ..
make -j$(sysctl -n hw.ncpu)
```

To create an app bundle: `macdeployqt build/Olive.app`

## Build (Arch Linux)

```bash
cd packaging/linux
makepkg -si
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
| macOS (arm64) | `Olive-0.1.3-arm64.dmg` |

Tested on Arch Linux only. Other builds are best-effort.

Build scripts in `packaging/linux/` (Dockerfiles, PKGBUILD) and `packaging/windows/` (cross-compile Dockerfile, NSIS).

## Upstream

Based on [olive-editor/olive](https://github.com/olive-editor/olive) by [MattKC](https://github.com/itsmattkc) and the Olive Team. Licensed under GPLv3.
