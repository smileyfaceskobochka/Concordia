#!/usr/bin/env bash

set -euo pipefail
cd "$(dirname "$0")"

# ──────────────── 💬 Help ────────────────
if [[ "${1:-}" == "--help" ]]; then
  cat <<EOF
Usage: ${0##*/} [--help]

Builds and runs the modular Vulkan+SDL3 triangle demo.

Options:
  --help    Show this help message and exit
EOF
  exit 0
fi

# ──────────────── 🔍 Dependency Lists ────────────────
required_cmds=(meson ninja glslc vulkaninfo pkg-config)
required_pkgs=(sdl3-dev)

# ──────────────── 📦 Package Managers ────────────────
package_managers=(apt dnf yum pacman zypper apk emerge nix brew snap flatpak)
declare -A install_cmd_map=(
  [apt]="install -y"
  [dnf]="install -y"
  [yum]="install -y"
  [pacman]="-S --noconfirm"
  [zypper]="install -y"
  [apk]="add"
  [emerge]="--ask=n"
  [nix]="install"
  [brew]="install"
  [snap]="install"
  [flatpak]="install"
)

# ──────────────── 🔗 Package Mappings ────────────────
declare -A pkg_map_apt=(
  [meson]="meson"
  [ninja]="ninja-build"
  [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools"
  [pkg-config]="pkg-config"
  [sdl3-dev]="libsdl3-dev"
)

declare -A pkg_map_pacman=(
  [meson]="meson"
  [ninja]="ninja"
  [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools"
  [pkg-config]="pkgconf"
  [sdl3-dev]="sdl3"
)

declare -A pkg_map_dnf=(
  [meson]="meson"
  [ninja]="ninja-build"
  [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools"
  [pkg-config]="pkgconf-pkg-config"
  [sdl3-dev]="SDL3-devel"
)

declare -A pkg_map_yum=(
  [meson]="meson"
  [ninja]="ninja-build"
  [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools"
  [pkg-config]="pkgconf-pkg-config"
  [sdl3-dev]="SDL3-devel"
)

declare -A pkg_map_zypper=(
  [meson]="meson"
  [ninja]="ninja"
  [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools"
  [pkg-config]="pkg-config"
  [sdl3-dev]="libSDL3-devel"
)

declare -A pkg_map_apk=(
  [meson]="meson"
  [ninja]="ninja"
  [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools"
  [pkg-config]="pkgconf"
  [sdl3-dev]="sdl3-dev"
)

# ──────────────── 🛠 Functions ────────────────
detect_installed_package_managers() {
  local found=()
  for mgr in "${package_managers[@]}"; do
    command -v "$mgr" &>/dev/null && found+=("$mgr")
  done
  echo "${found[@]}"
}

choose_package_manager() {
  local options=("$@")
  if ((${#options[@]} == 0)); then
    echo "❌ No supported package managers found." >&2
    exit 1
  elif ((${#options[@]} == 1)); then
    echo "${options[0]}"
  else
    echo "Multiple package managers detected. Select one:"
    select pm in "${options[@]}"; do
      [[ -n "$pm" ]] && echo "$pm" && break
    done
  fi
}

map_packages() {
  local mgr=$1; shift
  local deps=("$@")
  local map_name="pkg_map_$mgr"
  declare -n pkg_map="$map_name"
  local result=()

  for dep in "${deps[@]}"; do
    result+=("${pkg_map[$dep]:-$dep}")
  done

  echo "${result[@]}"
}

check_command() { command -v "$1" &>/dev/null; }
check_pkg_config() { pkg-config --exists "$1" 2>/dev/null; }

check_dependencies() {
  local missing=()
  for cmd in "${required_cmds[@]}"; do
    check_command "$cmd" || missing+=("$cmd")
  done
  for pkg in "${required_pkgs[@]}"; do
    check_pkg_config "$pkg" || missing+=("$pkg")
  done
  echo "${missing[@]}"
}

# ──────────────── 📦 Dependency Check ────────────────
mgrs=($(detect_installed_package_managers))
pkg_mgr=$(choose_package_manager "${mgrs[@]}")
echo "✅ Using package manager: $pkg_mgr"

missing=($(check_dependencies))
if ((${#missing[@]})); then
  echo "🔍 Missing dependencies: ${missing[*]}"
  read -rp "Install them automatically? [Y/n] " ans
  ans=${ans:-Y}
  if [[ "$ans" =~ ^[Yy]$ ]]; then
    install_cmd="${install_cmd_map[$pkg_mgr]}"
    pkgs=($(map_packages "$pkg_mgr" "${missing[@]}"))
    echo "📦 Installing: ${pkgs[*]}"
    if (( UID != 0 )); then
      sudo "$pkg_mgr" $install_cmd "${pkgs[@]}"
    else
      "$pkg_mgr" $install_cmd "${pkgs[@]}"
    fi
  else
    echo "🚫 Install dependencies manually and re-run."; exit 1
  fi
else
  echo "✅ All dependencies satisfied."
fi

# ──────────────── 🌐 Environment Setup ────────────────
: "${XDG_RUNTIME_DIR:=/run/user/$(id -u)}"
export XDG_RUNTIME_DIR
[[ -n "${WAYLAND_DISPLAY:-}" ]] && export SDL_VIDEODRIVER=wayland || export SDL_VIDEODRIVER=x11

echo "🌐 XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
echo "🌐 SDL_VIDEODRIVER=$SDL_VIDEODRIVER"

# ──────────────── 🛠 Build ────────────────
echo "🧱 Setting up Meson..."
meson setup build --wipe

echo "🔨 Compiling project..."
meson compile -C build

# ──────────────── 🚀 Run ────────────────
echo "🚀 Launching demo..."
build/src/triangle_engine
