ARG VERSION=0.1.3
ARG REVISION=1
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

ARG GIT_HASH

COPY src/ /src
WORKDIR /src/build

RUN cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      ${GIT_HASH:+-DGIT_HASH=${GIT_HASH}} .. && \
    make -j$(nproc)

# --- Packaging stage ---
FROM build AS package

ARG VERSION
ARG REVISION

RUN apt-get update && apt-get install -y dpkg-dev gettext-base && rm -rf /var/lib/apt/lists/*

COPY packaging/ /packaging/

RUN DESTDIR=/pkg make install && \
    export ARCH=$(dpkg --print-architecture) && \
    mkdir -p /pkg/DEBIAN /out && \
    envsubst < /packaging/linux/control.in > /pkg/DEBIAN/control && \
    dpkg-deb --build /pkg "/out/amber-editor_${VERSION}-${REVISION}_debian12_${ARCH}.deb"
