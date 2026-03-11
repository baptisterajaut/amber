FROM fedora:42 AS build

# Cross-compilation toolchain + Qt6 mingw packages + native Qt6 tools (lrelease, moc, etc.)
RUN dnf install -y \
    mingw64-gcc-c++ \
    mingw64-qt6-qtbase \
    mingw64-qt6-qtsvg \
    mingw64-qt6-qttools \
    mingw64-qt6-qtmultimedia \
    qt6-qttools \
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

COPY src/ /src
WORKDIR /src/build

RUN mingw64-cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)

# --- Packaging stage ---
FROM build AS package

RUN dnf install -y curl mingw64-nsis mingw-nsis-base && dnf clean all

COPY packaging/ /src/packaging/

# Collect everything into /out
RUN SYSROOT=/usr/x86_64-w64-mingw32/sys-root/mingw && \
    mkdir -p /out/platforms /out/imageformats /out/multimedia /out/effects /out/ts && \
    cp amber-editor.exe /out/ && \
    cp "${SYSROOT}/bin/"*.dll /out/ && \
    cp "${SYSROOT}/lib/qt6/plugins/platforms/qwindows.dll" /out/platforms/ && \
    cp "${SYSROOT}/lib/qt6/plugins/imageformats/"*.dll /out/imageformats/ 2>/dev/null || true && \
    cp "${SYSROOT}/lib/qt6/plugins/multimedia/"*.dll /out/multimedia/ 2>/dev/null || true && \
    cp /src/effects/shaders/*.xml /src/effects/shaders/*.frag /src/effects/shaders/*.vert /out/effects/ 2>/dev/null || true && \
    cp /src/build/*.qm /out/ts/ 2>/dev/null || true

# makensis hardcodes x86-unicode but mingw64-nsis ships amd64-unicode
RUN cd /usr/share/nsis/Stubs && \
    for f in *-amd64-unicode; do ln -sf "$f" "${f/amd64/x86}"; done && \
    ln -sf amd64-unicode /usr/share/nsis/Plugins/x86-unicode

# Build NSIS installer
RUN cd /src/packaging/windows/nsis && \
    cp -r /out amber && \
    curl -sL -o LICENSE "https://www.gnu.org/licenses/gpl-3.0.txt" && \
    makensis -DX64 amber.nsi && \
    cp *.exe /out/amber-setup.exe
