# Amber Video Editor

A free, open-source non-linear video editor built for speed and intuitivity. Sub-5 MB binary by standing on the shoulders of giants — runs on hardware that heavier NLEs have left behind.

Amber picks up where the amazing [Olive 0.1](https://github.com/olive-editor/olive) stopped in 2019. The entire rendering stack has been rebuilt (Qt RHI, FFmpeg 7/8, GPU decode), and active development continues toward 2.0.

> **AI-MAINTAINED CODEBASE**
>
> The original architecture, UX design, and rendering pipeline that make Amber so lightweight were hand-written by [MattKC](https://github.com/itsmattkc) and the Olive team back in 2019. That foundation is the real engineering. Everything since — the Qt 6 port, the RHI renderer, bug fixes, new features — is just human-assisted. If you are rebutted by this approach, I would respect your opinion, but I prefered an AI-revival over nothing at all.

![screen](.github/amber.jpg)

## Features

- **Multi-track timeline** with clip splitting, ripple/rolling/slip/slide tools, transitions, and keyframe animation
- **GPU-accelerated rendering** via Qt RHI (Vulkan, Metal, D3D12, OpenGL fallback) — no raw OpenGL calls in the codebase
- **Hardware video decoding** (VAAPI, D3D11VA, VideoToolbox) — enabled by default, software fallback automatic
- **Frei0r plugin support**

## Minimum requirements

- **GPU:** OpenGL 3.2+ (Intel HD / Sandy Bridge 2011+, any discrete GPU from the last 15 years). Vulkan/Metal/D3D12 used when available.
- **CPU:** any x86-64 processor.
- **RAM:** 512 MB free for simple edits; more helps with long timelines and high-res footage.
- **Linux:** Arch natively (PKGBUILD), everything else via AppImage (bundles Qt 6.10).
- **Windows:** 10 or newer. **macOS:** Apple Silicon; Intel Macs untested.

For GPUs older than OpenGL 3.2, use [version 1.1.0](https://github.com/baptisterajaut/amber/releases/tag/v1.1.0) (legacy OpenGL 2.x renderer).

## Roadmap

1.x is feature-complete, maintenance only. 2.0 is in active development — GPU-native effects, ShaderToy import, scopes, 3-point editing, rendering pipeline overhaul. See [ROADMAP.md](ROADMAP.md).

## Packages

Pre-built packages for Windows, Linux (AppImage) and macOS on the [Releases](https://github.com/baptisterajaut/amber/releases) page. Arch Linux: `makepkg -si` from `packaging/linux/PKGBUILD`. Tested on Arch only; other builds are best-effort.

## Build

```bash
# Linux
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# macOS
brew install qt@6 ffmpeg cmake
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix ffmpeg)"
cmake --build build -j$(sysctl -n hw.ncpu)

# Arch Linux
cd packaging/linux && makepkg -si

# Docker — AppImage (Qt 6.10, covers Ubuntu 24.04+)
docker buildx build -f packaging/linux/appimage.dockerfile --output type=local,dest=./out .

# Docker — Windows NSIS installer (cross-compiled from Fedora)
docker build -f packaging/windows/cross-compile.dockerfile --target package -t amber-win64 .
docker run --rm amber-win64 cat /out/amber-setup.exe > amber-setup.exe
```

**Dependencies:** Qt 6.10+ (Core, Gui, GuiPrivate, Widgets, Multimedia, OpenGL, OpenGLWidgets, ShaderTools, ShaderToolsPrivate, Svg, LinguistTools), FFmpeg 3.4–8 (avutil, avcodec, avformat, avfilter, swscale, swresample).

## License

GPLv3. Based on [olive-editor/olive](https://github.com/olive-editor/olive) by [MattKC](https://github.com/itsmattkc) and the Olive Team.
