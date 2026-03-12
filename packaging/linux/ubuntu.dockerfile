ARG VERSION=0.1.3
ARG REVISION=1

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    cmake build-essential \
    qt6-base-dev qt6-multimedia-dev qt6-tools-dev \
    libqt6opengl6-dev libqt6openglwidgets6 libqt6svg6-dev \
    libgl-dev \
    libavformat-dev libavcodec-dev libavutil-dev \
    libswscale-dev libswresample-dev libavfilter-dev \
    frei0r-plugins-dev \
    dpkg-dev gettext-base \
    && rm -rf /var/lib/apt/lists/*

ARG GIT_HASH
ARG VERSION
ARG REVISION

COPY src/ /src
COPY packaging/ /packaging/
WORKDIR /src/build

RUN cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      ${GIT_HASH:+-DGIT_HASH=${GIT_HASH}} .. && \
    make -j$(nproc)

RUN DESTDIR=/pkg make install && \
    export ARCH=$(dpkg --print-architecture) && \
    mkdir -p /pkg/DEBIAN /out && \
    envsubst < /packaging/linux/control.in > /pkg/DEBIAN/control && \
    dpkg-deb --build /pkg "/out/amber-editor_${VERSION}-${REVISION}_ubuntu2404_${ARCH}.deb"

FROM scratch
COPY --from=0 /out/ /
