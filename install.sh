#!/usr/bin/env bash
# Monix installer. Works both from a source checkout and from a release bundle
# (which ships prebuilt binaries in ./bin). Installs the kernel module (built for
# your running kernel — unavoidable), the app, the udev rule, and adds you to the
# 'audio' group.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); cd "$here"
say() { printf '\033[1;35m::\033[0m %s\n' "$*"; }
err() { printf '\033[1;31m!!\033[0m %s\n' "$*" >&2; }

# ---- detect distro + package set ------------------------------------------------
PM=""; PKGS=""
. /etc/os-release 2>/dev/null || true
case "${ID:-} ${ID_LIKE:-}" in
  *arch*)   PM="pacman"; PKGS="base-devel cmake git linux-headers alsa-lib glfw mesa dkms" ;;
  *debian*|*ubuntu*) PM="apt"; PKGS="build-essential cmake git linux-headers-$(uname -r) libasound2-dev libglfw3-dev libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev libwayland-dev dkms" ;;
  *fedora*|*rhel*)  PM="dnf"; PKGS="gcc-c++ cmake git kernel-devel alsa-lib-devel glfw-devel mesa-libGL-devel dkms" ;;
  *) err "Unknown distro — install build tools, kernel headers, ALSA/GLFW/OpenGL dev packages and dkms yourself." ;;
esac

if [ -n "$PM" ]; then
  say "Dependencies for ${PRETTY_NAME:-this system}:"
  echo "    $PKGS"
  read -r -p ":: Install them now with $PM? [Y/n] " a
  if [[ ! "$a" =~ ^[Nn] ]]; then
    case "$PM" in
      pacman) sudo pacman -S --needed $PKGS ;;
      apt)    sudo apt-get update && sudo apt-get install -y $PKGS ;;
      dnf)    sudo dnf install -y $PKGS ;;
    esac
  fi
fi

# ---- kernel module (DKMS preferred so it survives kernel upgrades) ---------------
VER=$(cat VERSION 2>/dev/null || echo 0.1)
if command -v dkms >/dev/null 2>&1; then
  say "Installing kernel module via DKMS (audient_id $VER)…"
  sudo rm -rf "/usr/src/audient_id-$VER"
  sudo cp -r driver "/usr/src/audient_id-$VER"
  sudo sed -i "s/^PACKAGE_VERSION=.*/PACKAGE_VERSION=\"$VER\"/" "/usr/src/audient_id-$VER/dkms.conf"
  sudo dkms add    -m audient_id -v "$VER" 2>/dev/null || true
  sudo dkms install -m audient_id -v "$VER" --force
else
  say "Building kernel module with make (no DKMS — rebuild after kernel updates)…"
  make -C driver
  sudo make -C driver install
fi
sudo cp driver/99-audient-id.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules || true
sudo modprobe audient_id || true

# ---- app: use prebuilt bin/ if present, else build from source ------------------
if [ -x bin/monix-gui ]; then
  say "Installing prebuilt app…"
  sudo install -Dm755 bin/monix-gui /usr/local/bin/monix-gui
  sudo install -Dm755 bin/monixctl  /usr/local/bin/monixctl
elif [ -f CMakeLists.txt ]; then
  say "Building app from source…"
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j"$(nproc)"
  sudo install -Dm755 build/app/monix-gui /usr/local/bin/monix-gui
  sudo install -Dm755 build/monixctl      /usr/local/bin/monixctl
else
  err "No prebuilt binary and no source to build."; exit 1
fi

# ---- access ---------------------------------------------------------------------
if ! id -nG "$USER" | grep -qw audio; then
  say "Adding $USER to the 'audio' group (log out/in for it to take effect)…"
  sudo usermod -aG audio "$USER"
fi

say "Done. Run 'monix-gui' (or 'monixctl status'). If it says permission denied,"
say "log out and back in so the 'audio' group applies."
