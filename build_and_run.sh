#!/usr/bin/env bash

set -euo pipefail
cd "$(dirname "$0")"

if [[ "${1:-}" == "--help" ]]; then
  cat <<EOF
Usage: ${0##*/} [--help]

Builds and runs the modular Vulkan+SDL3 triangle demo.

Options:
  --help    Show this help message and exit
EOF
  exit 0
fi

# ──────────────── Dependency definitions ────────────────

required_cmds=(meson ninja glslc vulkaninfo pkg-config)
required_pkgs=(sdl3 yaml-cpp)

package_managers=(
  apt dnf yum pacman zypper apk emerge nix brew snap flatpak
)
declare -A install_cmd_map=(
  [apt]="install -y" [dnf]="install -y" [yum]="install -y"
  [pacman]="-S --noconfirm" [zypper]="install -y" [apk]="add"
  [emerge]="--ask=n" [nix]="install" [brew]="install"
  [snap]="install" [flatpak]="install"
)

declare -A pkg_map_apt=(
  [meson]="meson" [ninja]="ninja-build" [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools" [pkg-config]="pkg-config"
  [sdl3]="libsdl3-dev" [yaml-cpp]="libyaml-cpp-dev"
)
declare -A pkg_map_dnf=(
  [meson]="meson" [ninja]="ninja-build" [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools" [pkg-config]="pkgconf-pkg-config"
  [sdl3]="SDL3-devel" [yaml-cpp]="yaml-cpp-devel"
)
declare -A pkg_map_yum="${pkg_map_dnf[@]}"
declare -A pkg_map_pacman=(
  [meson]="meson" [ninja]="ninja" [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools" [pkg-config]="pkgconf"
  [sdl3]="sdl3" [yaml-cpp]="yaml-cpp"
)
declare -A pkg_map_zypper=(
  [meson]="meson" [ninja]="ninja" [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools" [pkg-config]="pkg-config"
  [sdl3]="libSDL3-devel" [yaml-cpp]="yaml-cpp-devel"
)
declare -A pkg_map_apk=(
  [meson]="meson" [ninja]="ninja" [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools" [pkg-config]="pkgconf"
  [sdl3]="sdl3-dev" [yaml-cpp]="yaml-cpp-dev"
)

detect_installed_package_managers() {
  for m in "${package_managers[@]}"; do
    command -v "$m" &>/dev/null && echo "$m"
  done
}

choose_package_manager() {
  local opts=("$@")
  if ((${#opts[@]} == 1)); then
    echo "${opts[0]}"
  else
    echo "Multiple package managers found. Please select one:"
    select pm in "${opts[@]}"; do
      [[ -n "$pm" ]] && { echo "$pm"; break; }
    done
  fi
}

check_command() { command -v "$1" &>/dev/null; }
check_pkg()     { pkg-config --exists "$1" 2>/dev/null; }

check_dependencies() {
  local miss=()
  for cmd in "${required_cmds[@]}"; do
    check_command "$cmd" || miss+=("$cmd")
  done
  for pkg in "${required_pkgs[@]}"; do
    check_pkg "$pkg" || miss+=("$pkg")
  done
  echo "${miss[@]}"
}

map_packages() {
  local mgr=$1; shift
  local deps=("$@") result=()
  local map_var="pkg_map_$mgr"; declare -n pm=$map_var
  for d in "${deps[@]}"; do
    result+=("${pm[$d]:-$d}")
  done
  echo "${result[@]}"
}

main() {
  echo "Checking for required tools and libraries..."
  mapfile -t mgrs < <(detect_installed_package_managers)
  pkg_mgr=$(choose_package_manager "${mgrs[@]}")
  echo "Using package manager: $pkg_mgr"

  IFS=' ' read -r -a missing <<< "$(check_dependencies)"
  if ((${#missing[@]})); then
    echo "Missing dependencies: ${missing[*]}"
    read -rp "Install missing automatically? [Y/n] " ans
    ans=${ans:-Y}
    if [[ $ans =~ ^[Yy]$ ]]; then
      install_cmd=${install_cmd_map[$pkg_mgr]}
      mapfile -t to_install < <(map_packages "$pkg_mgr" "${missing[@]}")
      echo "Installing: ${to_install[*]}"
      if (( UID != 0 )); then
        sudo "$pkg_mgr" $install_cmd "${to_install[@]}"
      else
        "$pkg_mgr" $install_cmd "${to_install[@]}"
      fi
    else
      echo "Aborted by user."; exit 1
    fi
  else
    echo "All dependencies satisfied."
  fi

  export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
  export SDL_VIDEODRIVER="${WAYLAND_DISPLAY:+wayland}"
  export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
  echo "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
  echo "SDL_VIDEODRIVER=$SDL_VIDEODRIVER"

  echo "Configuring Meson..."
  meson setup build --wipe

  echo "Building project..."
  meson compile -C build

  # copy config into build so engine can find it
  cp -r config build/

  export LD_LIBRARY_PATH="/usr/lib:${LD_LIBRARY_PATH:-}"

  echo "Launching demo from build/ ..."
  pushd build > /dev/null
  ./src/triangle_engine config/engine_config.yaml
  popd > /dev/null
}

main "$@"
