# The Linux build from Windows: Ubuntu 24.04 as in CI, GCC 14, CMake 4, the system's mpv and
# aria2, a throwaway keyring for the credential test and the packaging tools. Qt goes once into a
# volume:
#   docker build -t animehub-linux-build:24.04 -f scripts/linux-build.Dockerfile scripts
#   docker run --rm -v animehub-qt-cache:/opt/qt animehub-linux-build:24.04 \
#     aqt install-qt linux desktop 6.11.0 linux_gcc_64 -m qtimageformats -O /opt/qt
# then, with that volume at /opt/qt: QT_ROOT=/opt/qt/6.11.0/gcc_64 and the linux-* presets, the
# test command and scripts/package-linux.sh as .github/workflows run them.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++-14 gcc-14 ninja-build pkg-config patchelf file git curl ca-certificates xz-utils \
      python3-pip python3-venv dpkg-dev \
      libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0 libfontconfig1 libfreetype6 \
      libdbus-1-3 libglib2.0-0t64 \
      libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
      libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxcb-xinput0 \
      libx11-xcb1 libxrender1 libxi6 libsm6 libice6 \
      libssl-dev libsecret-1-dev \
      mpv aria2 fonts-dejavu-core \
      dbus dbus-x11 gnome-keyring libsecret-tools \
      xvfb x11-utils \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-14 100

RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir cmake==4.4.2 aqtinstall==3.3.0
ENV PATH=/opt/venv/bin:$PATH
