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

## Build (Linux)

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

**Dependencies:** Qt 6 (Core, Gui, Widgets, Multimedia, OpenGL, OpenGLWidgets, Svg, LinguistTools), FFmpeg 3.4–8 (avutil, avcodec, avformat, avfilter, swscale, swresample), OpenGL.

## Build (Windows cross-compile)

```bash
docker build -f packaging/windows/cross-compile.dockerfile -t olive-win64 .
docker run --rm -v $(pwd)/out:/host olive-win64 cp -r /out /host/olive-eva01
```

## Packages

Linux `.deb`, Arch PKGBUILD, and AppImage build scripts in `packaging/linux/`.

## Upstream

Based on [olive-editor/olive](https://github.com/olive-editor/olive) by Matt Konecny and the Olive Team. Licensed under GPLv3.
