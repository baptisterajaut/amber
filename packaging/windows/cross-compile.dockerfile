FROM fedora:42 AS build

# Cross-compilation toolchain + Qt6 mingw packages + native Qt6 tools (lrelease, moc, etc.)
RUN dnf install -y \
    mingw64-gcc-c++ \
    mingw64-qt6-qtbase \
    mingw64-qt6-qtsvg \
    mingw64-qt6-qttools \
    mingw64-qt6-qtmultimedia \
    mingw64-qt6-qtshadertools \
    qt6-qtbase-devel \
    qt6-qttools-devel \
    qt6-qtshadertools-devel \
    cmake \
    make \
    && dnf clean all

# Download pre-built FFmpeg 7.1 shared libs (MinGW, GPL, includes all common codecs)
ARG FFMPEG_ARCHIVE=ffmpeg-n7.1-latest-win64-gpl-shared-7.1
RUN dnf install -y unzip curl && \
    curl -L -o /tmp/ffmpeg.zip \
    "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/${FFMPEG_ARCHIVE}.zip" && \
    unzip /tmp/ffmpeg.zip -d /tmp && \
    SYSROOT=/usr/x86_64-w64-mingw32/sys-root/mingw && \
    cp -r /tmp/${FFMPEG_ARCHIVE}/include/* "${SYSROOT}/include/" && \
    cp /tmp/${FFMPEG_ARCHIVE}/lib/*.dll.a "${SYSROOT}/lib/" && \
    mkdir -p "${SYSROOT}/lib/pkgconfig" && \
    cp /tmp/${FFMPEG_ARCHIVE}/lib/pkgconfig/*.pc "${SYSROOT}/lib/pkgconfig/" && \
    cp /tmp/${FFMPEG_ARCHIVE}/bin/*.dll "${SYSROOT}/bin/" && \
    rm -rf /tmp/ffmpeg*

ARG GIT_HASH
ARG VERSION=dev

COPY src/ /src
COPY packaging/ /packaging
COPY LICENSE /LICENSE
WORKDIR /src/build

RUN mingw64-cmake -DCMAKE_BUILD_TYPE=Release \
      ${GIT_HASH:+-DGIT_HASH=${GIT_HASH}} \
      -DAMBER_VERSION=${VERSION} \
      -DQT_HOST_PATH=/usr \
      -DQT_HOST_PATH_CMAKE_DIR=/usr/lib64/cmake .. && \
    make -j$(nproc)

# --- Packaging stage ---
FROM build AS package

RUN dnf install -y curl unzip zip mingw64-nsis mingw-nsis-base && dnf clean all

COPY packaging/ /packaging/

# Download pre-built Frei0r plugins for Windows
RUN curl -sL -o /tmp/frei0r.zip \
      "https://github.com/dyne/frei0r/releases/download/v2.5.4/frei0r-v2.5.4_win64.zip" && \
    mkdir -p /tmp/frei0r && cd /tmp/frei0r && unzip -q /tmp/frei0r.zip && \
    rm /tmp/frei0r.zip

# Collect everything into /out
RUN SYSROOT=/usr/x86_64-w64-mingw32/sys-root/mingw && \
    mkdir -p /out/platforms /out/imageformats /out/multimedia /out/effects /out/ts && \
    cp amber-editor.exe /out/ && \
    cp "${SYSROOT}/bin/"*.dll /out/ && \
    cp "${SYSROOT}/lib/qt6/plugins/platforms/qwindows.dll" /out/platforms/ && \
    cp "${SYSROOT}/lib/qt6/plugins/imageformats/"*.dll /out/imageformats/ 2>/dev/null || true && \
    cp "${SYSROOT}/lib/qt6/plugins/multimedia/"*.dll /out/multimedia/ 2>/dev/null || true && \
    cp /src/effects/shaders/*.xml /src/effects/shaders/*.frag /src/effects/shaders/*.vert /out/effects/ 2>/dev/null || true && \
    cp -r /tmp/frei0r/frei0r-*/. /out/effects/frei0r-1/ 2>/dev/null || true && \
    cp /src/build/*.qm /out/ts/ 2>/dev/null || true && \
    mkdir -p /out/exportpresets && \
    cp /src/exportpresets/* /out/exportpresets/ 2>/dev/null || true

# makensis hardcodes x86-unicode but mingw64-nsis ships amd64-unicode
RUN cd /usr/share/nsis/Stubs && \
    for f in *-amd64-unicode; do ln -sf "$f" "${f/amd64/x86}"; done && \
    ln -sf amd64-unicode /usr/share/nsis/Plugins/x86-unicode

# Build NSIS installer
RUN cd /packaging/windows/nsis && \
    cp -r /out amber && \
    cp /LICENSE . && \
    makensis -DX64 amber.nsi && \
    cp *.exe /out/amber-setup.exe

# Build portable zip (same content as NSIS + LICENSE)
RUN cd /packaging/windows/nsis && \
    cp /LICENSE amber/ && \
    zip -r /out/amber-portable.zip amber/

# --- Output stage (for --output) ---
FROM scratch AS installer
COPY --from=package /out/amber-setup.exe /amber-setup.exe
COPY --from=package /out/amber-portable.zip /amber-portable.zip
