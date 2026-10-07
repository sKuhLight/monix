#!/usr/bin/env bash
# Monix installer. Works from a source checkout or from a release bundle with
# prebuilt binaries in ./bin. Installs the app, DKMS-capable driver source, the
# driver maintenance helper, udev rules, and a desktop launcher.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); cd "$here"
say() { printf '\033[1;35m::\033[0m %s\n' "$*"; }
err() { printf '\033[1;31m!!\033[0m %s\n' "$*" >&2; }
have() { command -v "$1" >/dev/null 2>&1; }

WITH_DEPS=yes
WITH_DRIVER=yes
WITH_APP=yes
while [ $# -gt 0 ]; do
  case "$1" in
    --no-deps) WITH_DEPS=no ;;
    --driver-only) WITH_APP=no ;;
    --app-only) WITH_DRIVER=no ;;
    -h|--help)
      echo "Usage: ./install.sh [--no-deps] [--driver-only|--app-only]"
      exit 0 ;;
    *) err "Unknown option: $1"; exit 2 ;;
  esac
  shift
done

VER=$(cat VERSION 2>/dev/null || echo 0.1)
DRIVERCTL="$here/tools/monix-driverctl"

if [ "$WITH_DEPS" = yes ]; then
  say "Dependency check"
  echo "    Monix can install build/runtime dependencies for Arch, Debian/Ubuntu, and Fedora/RHEL."
  read -r -p ":: Install/update dependencies now? [Y/n] " a
  if [[ ! "$a" =~ ^[Nn] ]]; then
    MONIX_VERSION="$VER" "$DRIVERCTL" install-deps
  fi
fi

say "Installing driver maintenance helper"
sudo install -Dm755 "$DRIVERCTL" /usr/local/bin/monix-driverctl
sudo rm -rf /usr/share/monix/driver
sudo install -d /usr/share/monix
sudo cp -r driver /usr/share/monix/driver

if [ "$WITH_DRIVER" = yes ]; then
  say "Installing/rebuilding kernel helper driver"
  MONIX_VERSION="$VER" "$DRIVERCTL" install-current --no-deps
fi

if [ "$WITH_APP" = yes ]; then
  if [ -x bin/monix-gui ]; then
    say "Installing prebuilt app"
    sudo install -Dm755 bin/monix-gui /usr/local/bin/monix-gui
    sudo install -Dm755 bin/monixctl  /usr/local/bin/monixctl
  elif [ -f CMakeLists.txt ]; then
    say "Building app from source"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j"$(nproc)"
    sudo install -Dm755 build/app/monix-gui /usr/local/bin/monix-gui
    sudo install -Dm755 build/monixctl      /usr/local/bin/monixctl
  else
    err "No prebuilt binary and no source to build."; exit 1
  fi

  say "Installing desktop launcher"
  tmp=$(mktemp)
  cat > "$tmp" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Monix
Comment=Audient iD control panel
Exec=monix-gui
Icon=audio-card
Terminal=false
StartupWMClass=monix
Categories=Audio;AudioVideo;Mixer;
DESKTOP
  sudo install -Dm644 "$tmp" /usr/local/share/applications/monix.desktop
  rm -f "$tmp"
fi

if getent group audio >/dev/null && ! id -nG "$USER" | grep -qw audio; then
  say "Adding $USER to the 'audio' group (log out/in for it to take effect)"
  sudo usermod -aG audio "$USER"
fi

say "Done. Run 'monix-gui' or launch Monix from your app menu."
say "The app can now detect a missing current-kernel driver and offer to rebuild it."
