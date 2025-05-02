#!/usr/bin/env bash
# Portable Bash script for building & running Vulkan+SDL3 triangle
# Usage: ./build-and-run.sh [--help]

set -euo pipefail

show_help() {
  cat <<EOF
Usage: ${0##*/} [--help]

Builds the project (Meson + Ninja), installs missing deps (if sudo available),
sets up environment, and runs the Vulkan+SDL3 triangle demo.

Options:
  --help    Show this help message and exit
EOF
}

if [[ "${1:-}" == "--help" ]]; then
  show_help
  exit 0
fi

detect_distro() {
  if command -v lsb_release &>/dev/null; then
    distro=$(lsb_release -is)
  elif [ -f /etc/os-release ]; then
    . /etc/os-release
    distro=$ID
  else
    distro="unknown"
  fi
  echo "$distro"
}

distro=$(detect_distro)
echo "Detected Linux distro: $distro"

deps=(meson ninja glslc SDL3-config vulkaninfo)

missing=()
for cmd in "${deps[@]}"; do
  if ! command -v "$cmd" &>/dev/null; then
    missing+=("$cmd")
  fi
done

if (( ${#missing[@]} )); then
  echo "Missing commands: ${missing[*]}"
  if [ "$(id -u)" -eq 0 ] || command -v sudo &>/dev/null; then
    read -p "Install missing packages? [Y/n] " ans
    ans=${ans:-Y}
    if [[ $ans =~ ^[Yy]$ ]]; then
      case "$distro" in
        Ubuntu|Debian)
          pkg_mgr="apt"; pkg_install="install -y";;
        Fedora|RHEL|CentOS)
          pkg_mgr="dnf"; pkg_install="install -y";;
        Arch)
          pkg_mgr="pacman"; pkg_install="-S --noconfirm";;
        Alpine)
          pkg_mgr="apk"; pkg_install="add";;
        *)
          echo "Unsupported distro for auto‑install"; exit 1;;
      esac
      echo "Running package manager: $pkg_mgr ${pkg_install} ${missing[*]}"
      sudo $pkg_mgr $pkg_install "${missing[@]}"
    fi
  else
    echo "Please install missing dependencies manually."
    exit 1
  fi
fi

: "${XDG_RUNTIME_DIR:=/run/user/$(id -u)}"
export XDG_RUNTIME_DIR
echo "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"

if [ -n "${WAYLAND_DISPLAY:-}" ]; then
  export SDL_VIDEODRIVER=wayland
else
  export SDL_VIDEODRIVER=x11
fi
echo "SDL_VIDEODRIVER=$SDL_VIDEODRIVER"

meson setup build --wipe
ninja -C build

# 8) Run the demo
cd build
./triangle
