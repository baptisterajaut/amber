FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    cmake build-essential \
    qt6-base-dev qt6-multimedia-dev qt6-tools-dev \
    libqt6opengl6-dev libqt6openglwidgets6 libqt6svg6-dev \
    libgl-dev \
    libavformat-dev libavcodec-dev libavutil-dev \
    libswscale-dev libswresample-dev libavfilter-dev \
    frei0r-plugins-dev \
    && rm -rf /var/lib/apt/lists/*

COPY . /src
WORKDIR /src/build

RUN cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr .. && \
    make -j$(nproc)

# --- .deb (Ubuntu 24.04) ---
FROM build AS deb

ARG VERSION=0.1.3
ARG REVISION=1

RUN apt-get update && apt-get install -y dpkg-dev && rm -rf /var/lib/apt/lists/*

RUN DESTDIR=/pkg make install && \
    ARCH=$(dpkg --print-architecture) && \
    mkdir -p /pkg/DEBIAN /out && \
    echo "Package: olive-editor" > /pkg/DEBIAN/control && \
    echo "Version: ${VERSION}-${REVISION}" >> /pkg/DEBIAN/control && \
    echo "Section: video" >> /pkg/DEBIAN/control && \
    echo "Priority: optional" >> /pkg/DEBIAN/control && \
    echo "Architecture: ${ARCH}" >> /pkg/DEBIAN/control && \
    echo "Depends: libqt6multimedia6, libqt6openglwidgets6, libqt6svg6, libavformat60 | libavformat61, libavcodec60 | libavcodec61, libavutil58 | libavutil59, libswscale7 | libswscale8, libswresample4 | libswresample5, libavfilter9 | libavfilter10, libgl1" >> /pkg/DEBIAN/control && \
    echo "Maintainer: Olive Team <itsmattkc@gmail.com>" >> /pkg/DEBIAN/control && \
    echo "Description: Professional open-source non-linear video editor" >> /pkg/DEBIAN/control && \
    dpkg-deb --build /pkg "/out/olive-editor_${VERSION}-${REVISION}_ubuntu2404_${ARCH}.deb"

# --- AppImage ---
FROM build AS appimage

RUN apt-get update && apt-get install -y curl file libfuse2 && rm -rf /var/lib/apt/lists/*

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

RUN DESTDIR=/tmp/AppDir make install && \
    VERSION="${VERSION:-0.1.3}" \
    linuxdeploy \
    --appdir /tmp/AppDir \
    --plugin qt \
    --output appimage \
    --desktop-file /tmp/AppDir/usr/share/applications/org.olivevideoeditor.Olive.desktop \
    --icon-file /tmp/AppDir/usr/share/icons/hicolor/256x256/apps/org.olivevideoeditor.Olive.png

RUN mkdir -p /out && mv /src/build/Olive*.AppImage /out/ 2>/dev/null || mv /src/build/*.AppImage /out/ 2>/dev/null || true
