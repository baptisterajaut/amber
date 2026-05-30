# Fedora Prerequisites for Amber Video Editor

To build Amber on Fedora Linux, you must ensure your system meets the minimum hardware requirements and has the necessary Qt 6 and FFmpeg development packages installed. The following has been tested on Fedora 44.

## Required Dependencies

Amber requires **Qt 6.10+** (specifically the Core, Gui, GuiPrivate, Widgets, Multimedia, OpenGL, OpenGLWidgets, ShaderTools, ShaderToolsPrivate, Svg, and LinguistTools modules) alongside **FFmpeg 3.4–8**.

Run the following command to install the complete build environment and all required headers on Fedora:

```bash
sudo dnf install \
  qt6-qtbase-devel \
  qt6-qtbase-private-devel \
  qt6-qtmultimedia-devel \
  qt6-qtshadertools-devel \
  qt6-qtsvg-devel \
  qt6-qttools-devel \
  ffmpeg-free-devel \
  cmake gcc-c++ make -y
```

> **Note:** If you are utilizing the RPM Fusion repository for full codec support, you can replace `ffmpeg-free-devel` with `ffmpeg-devel`.

## Build Instructions

Once all prerequisites are installed, you can configure and build the project from the source directory:

```bash
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Wayland & GNOME Compatibility

Amber includes native compatibility workarounds for running under GNOME Wayland sessions. GNOME's compositor (Mutter) does not support Server-Side Decorations (SSD). Amber automatically detects GNOME Wayland environments at startup and transparently falls back to XWayland (`xcb`) to restore native window title bars and borders.
