ARG VERSION=0.1.3
ARG REVISION=1

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

ARG GIT_HASH

COPY src/ /src
WORKDIR /src/build

RUN cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      ${GIT_HASH:+-DGIT_HASH=${GIT_HASH}} .. && \
    make -j$(nproc)

# --- .deb (Ubuntu 24.04) ---
FROM build AS deb

ARG VERSION
ARG REVISION

RUN apt-get update && apt-get install -y dpkg-dev gettext-base && rm -rf /var/lib/apt/lists/*

COPY packaging/ /packaging/

RUN DESTDIR=/pkg make install && \
    export ARCH=$(dpkg --print-architecture) && \
    mkdir -p /pkg/DEBIAN /out && \
    envsubst < /packaging/linux/control.in > /pkg/DEBIAN/control && \
    dpkg-deb --build /pkg "/out/amber-editor_${VERSION}-${REVISION}_ubuntu2404_${ARCH}.deb"

# --- AppImage ---
FROM build AS appimage

ARG VERSION

RUN apt-get update && apt-get install -y curl file libfuse2 && rm -rf /var/lib/apt/lists/*

COPY packaging/ /packaging/

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
    VERSION="${VERSION}" \
    linuxdeploy \
    --appdir /tmp/AppDir \
    --plugin qt \
    --output appimage \
    --desktop-file /tmp/AppDir/usr/share/applications/org.ambervideoeditor.Amber.desktop \
    --icon-file /tmp/AppDir/usr/share/icons/hicolor/256x256/apps/org.ambervideoeditor.Amber.png

RUN mkdir -p /out && mv /src/build/Amber*.AppImage /out/ 2>/dev/null || mv /src/build/*.AppImage /out/ 2>/dev/null || true

# --- Both (.deb + AppImage) ---
FROM scratch AS both
COPY --from=deb /out/ /
COPY --from=appimage /out/ /
