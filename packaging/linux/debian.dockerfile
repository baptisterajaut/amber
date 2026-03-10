ARG BASE_IMAGE=debian:12
FROM ${BASE_IMAGE} AS build

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
    make -j$(nproc) && \
    DESTDIR=/pkg make install

# --- Packaging stage ---
FROM build AS package

ARG VERSION=0.1.3
ARG REVISION=1

RUN apt-get update && apt-get install -y dpkg-dev && rm -rf /var/lib/apt/lists/*

RUN ARCH=$(dpkg --print-architecture) && \
    mkdir -p /pkg/DEBIAN /out && \
    echo "Package: olive-editor" > /pkg/DEBIAN/control && \
    echo "Version: ${VERSION}-${REVISION}" >> /pkg/DEBIAN/control && \
    echo "Section: video" >> /pkg/DEBIAN/control && \
    echo "Priority: optional" >> /pkg/DEBIAN/control && \
    echo "Architecture: ${ARCH}" >> /pkg/DEBIAN/control && \
    echo "Depends: libqt6multimedia6, libqt6openglwidgets6, libqt6svg6, libavformat59 | libavformat60 | libavformat61, libavcodec59 | libavcodec60 | libavcodec61, libavutil57 | libavutil58 | libavutil59, libswscale6 | libswscale7 | libswscale8, libswresample4 | libswresample5, libavfilter8 | libavfilter9 | libavfilter10, libgl1" >> /pkg/DEBIAN/control && \
    echo "Maintainer: Olive Team <itsmattkc@gmail.com>" >> /pkg/DEBIAN/control && \
    echo "Description: Professional open-source non-linear video editor" >> /pkg/DEBIAN/control && \
    dpkg-deb --build /pkg "/out/olive-editor_${VERSION}-${REVISION}_${ARCH}.deb"
