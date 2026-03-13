# Dev image for iterative AppImage builds with Qt 6.10 (PipeWire native backend).
# Usage:
#   docker build -f packaging/linux/dev.dockerfile -t amber-dev .   (one-time)
#   docker run --rm \
#     -v ./src:/src -v ./packaging:/packaging -v amber-build:/src/build -v ./out:/out \
#     amber-dev
#
# The named volume `amber-build` persists the cmake cache between runs,
# so only changed files are recompiled.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# System deps: xcb/GL/FFmpeg (Qt itself comes from aqtinstall below)
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

# Qt 6.10.2 via aqtinstall (includes native PipeWire audio backend)
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

WORKDIR /src/build

CMD ["bash", "-c", "\
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_PREFIX_PATH=${Qt6_DIR} .. && \
    make -j$(nproc) && \
    rm -rf /tmp/AppDir && \
    DESTDIR=/tmp/AppDir make install && \
    rm -f ${Qt6_DIR}/plugins/multimedia/libffmpegmediaplugin.so && \
    VERSION=${VERSION:-1.1.0} \
    QMAKE=${Qt6_DIR}/bin/qmake \
    linuxdeploy \
      --appdir /tmp/AppDir \
      --plugin qt \
      --output appimage \
      --exclude-library 'libva*' \
      --desktop-file /tmp/AppDir/usr/share/applications/org.ambervideoeditor.Amber.desktop \
      --icon-file /tmp/AppDir/usr/share/icons/hicolor/256x256/apps/org.ambervideoeditor.Amber.png && \
    mkdir -p /out && \
    mv Amber*.AppImage /out/ 2>/dev/null || mv *.AppImage /out/ 2>/dev/null || true \
"]
