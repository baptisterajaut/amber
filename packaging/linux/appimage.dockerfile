# AppImage build with Qt 6.10 (native PipeWire audio backend).
# Ubuntu 24.04 base for glibc 2.39 portability.
#
# Usage:
#   docker build -f packaging/linux/appimage.dockerfile -t amber-appimage \
#     --build-arg GIT_HASH=$(git rev-parse --short HEAD) .
#   docker run --rm -v ./out:/out amber-appimage

ARG VERSION=1.1.0

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# System deps: xcb/GL/FFmpeg (Qt comes from aqtinstall below)
RUN apt-get update && apt-get install -y \
    cmake build-essential python3-pip \
    libgl-dev libegl-dev \
    libfontconfig1-dev libfreetype-dev \
    libx11-dev libx11-xcb-dev libxcb1-dev \
    libxcb-glx0-dev libxcb-cursor-dev \
    libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev \
    libxcb-randr0-dev libxcb-render-util0-dev libxcb-shape0-dev \
    libxcb-shm0-dev libxcb-sync-dev libxcb-util-dev \
    libxcb-xfixes0-dev libxcb-xkb-dev \
    libxext-dev libxfixes-dev libxi-dev libxrandr-dev \
    libxkbcommon-dev libxkbcommon-x11-dev libxrender-dev \
    libavformat-dev libavcodec-dev libavutil-dev \
    libswscale-dev libswresample-dev libavfilter-dev \
    frei0r-plugins-dev \
    curl file libfuse2 \
    && rm -rf /var/lib/apt/lists/*

# Qt 6.10.2 via aqtinstall (native PipeWire audio backend)
RUN rm -f /usr/lib/python3.*/EXTERNALLY-MANAGED && \
    pip install aqtinstall && \
    aqt install-qt linux desktop 6.10.2 linux_gcc_64 \
      -m qtmultimedia \
      --outputdir /opt/qt && \
    pip uninstall -y aqtinstall && \
    rm -rf /root/.cache/pip

ENV Qt6_DIR=/opt/qt/6.10.2/gcc_64
ENV CMAKE_PREFIX_PATH=${Qt6_DIR}
ENV PATH=${Qt6_DIR}/bin:${PATH}
ENV LD_LIBRARY_PATH=${Qt6_DIR}/lib

# linuxdeploy + Qt plugin (extracted, no FUSE needed)
RUN curl -L -o /tmp/linuxdeploy.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage && \
    chmod +x /tmp/linuxdeploy.AppImage && \
    cd /tmp && /tmp/linuxdeploy.AppImage --appimage-extract && \
    mv /tmp/squashfs-root /opt/linuxdeploy && \
    ln -s /opt/linuxdeploy/AppRun /usr/local/bin/linuxdeploy && \
    curl -L -o /tmp/plugin-qt.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage && \
    chmod +x /tmp/plugin-qt.AppImage && \
    cd /tmp && /tmp/plugin-qt.AppImage --appimage-extract && \
    mv /tmp/squashfs-root /opt/linuxdeploy-plugin-qt && \
    ln -s /opt/linuxdeploy-plugin-qt/AppRun /usr/local/bin/linuxdeploy-plugin-qt && \
    rm -f /tmp/*.AppImage

ARG GIT_HASH
ARG VERSION

COPY src/ /src
COPY packaging/ /packaging
WORKDIR /src/build

RUN cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_PREFIX_PATH=${Qt6_DIR} \
      ${GIT_HASH:+-DGIT_HASH=${GIT_HASH}} .. && \
    make -j$(nproc)

# Remove Qt's FFmpeg media plugin before bundling — we use our own FFmpeg
# for decoding and only need Qt Multimedia for QAudioSink (PipeWire backend).
# Leaving it would pull in a second FFmpeg version (ABI conflict).
RUN DESTDIR=/tmp/AppDir make install && \
    rm -f ${Qt6_DIR}/plugins/multimedia/libffmpegmediaplugin.so && \
    VERSION="${VERSION}" \
    QMAKE=${Qt6_DIR}/bin/qmake \
    linuxdeploy \
      --appdir /tmp/AppDir \
      --plugin qt \
      --output appimage \
      --exclude-library "libva*" \
      --desktop-file /tmp/AppDir/usr/share/applications/org.ambervideoeditor.Amber.desktop \
      --icon-file /tmp/AppDir/usr/share/icons/hicolor/256x256/apps/org.ambervideoeditor.Amber.png

RUN mkdir -p /out && mv /src/build/Amber*.AppImage /out/ 2>/dev/null || mv /src/build/*.AppImage /out/ 2>/dev/null || true

FROM scratch
COPY --from=0 /out/ /
