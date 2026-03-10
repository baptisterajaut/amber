FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    cmake build-essential git curl file \
    qt6-base-dev qt6-multimedia-dev qt6-tools-dev \
    libqt6opengl6-dev libqt6openglwidgets6 libqt6svg6-dev \
    libgl-dev \
    libavformat-dev libavcodec-dev libavutil-dev \
    libswscale-dev libswresample-dev libavfilter-dev \
    frei0r-plugins-dev \
    libfuse2 \
    && rm -rf /var/lib/apt/lists/*

# Download and extract linuxdeploy + Qt plugin (AppImages can't run in Docker without FUSE)
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
    ln -s /opt/linuxdeploy-plugin-qt/AppRun /usr/local/bin/linuxdeploy-plugin-qt

COPY . /src
WORKDIR /src/build

RUN cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr .. && \
    make -j$(nproc) && \
    DESTDIR=/tmp/AppDir make install

RUN VERSION="${VERSION:-0.1.3}" \
    linuxdeploy \
    --appdir /tmp/AppDir \
    --plugin qt \
    --output appimage \
    --desktop-file /tmp/AppDir/usr/share/applications/org.olivevideoeditor.Olive.desktop \
    --icon-file /tmp/AppDir/usr/share/icons/hicolor/256x256/apps/org.olivevideoeditor.Olive.png

RUN mkdir -p /out && mv /src/build/Olive*.AppImage /out/ 2>/dev/null || mv /src/build/*.AppImage /out/ 2>/dev/null || true
