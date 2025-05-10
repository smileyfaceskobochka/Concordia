#!/usr/bin/env bash

set -euo pipefail

# Ensure script runs from project root directory
cd "$(dirname "$0")"

# Check shaders exist
if [[ ! -d "shaders" || ! -f "shaders/vert.glsl" || ! -f "shaders/frag.glsl" ]]; then
  echo "Error: Shader files missing in 'shaders/' directory."
  exit 1
fi

show_help() {
  cat <<EOF
Usage: ${0##*/} [--help]

Builds and runs the Vulkan+SDL3 triangle demo.

Options:
  --help    Show this help message and exit
EOF
}

if [[ "${1:-}" == "--help" ]]; then
  show_help
  exit 0
fi

# List of known package managers in order of preference
package_managers=(apt dnf yum pacman zypper apk emerge nix brew snap flatpak)

# --- Detect installed package managers ---
detect_installed_package_managers() {
  local mgrs=()
  for mgr in "${package_managers[@]}"; do
    if command -v "$mgr" &>/dev/null; then
      mgrs+=("$mgr")
    fi
  done
  echo "${mgrs[@]}"
}

# --- Prompt user to select package manager if multiple found ---
choose_package_manager() {
  local options=("$@")
  if ((${#options[@]} == 0)); then
    echo "No supported package managers found on this system."
    exit 1
  elif ((${#options[@]} == 1)); then
    echo "${options[0]}"
  else
    echo "Multiple package managers detected. Please select one:"
    select pm in "${options[@]}"; do
      if [[ -n "$pm" ]]; then
        echo "$pm"
        break
      else
        echo "Invalid selection. Please try again."
      fi
    done
  fi
}

# --- Map package manager to install command ---
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

# --- Package name mappings per package manager ---
declare -A pkg_map_apt=(
  [meson]="meson"
  [ninja]="ninja-build"
  [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools"
  [pkg-config]="pkg-config"
  [sdl3-dev]="libsdl3-dev"
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

declare -A pkg_map_pacman=(
  [meson]="meson"
  [ninja]="ninja"
  [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools"
  [pkg-config]="pkgconf"
  [sdl3-dev]="sdl3"
)

declare -A pkg_map_apk=(
  [meson]="meson"
  [ninja]="ninja"
  [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools"
  [pkg-config]="pkgconf"
  [sdl3-dev]="sdl3-dev"
)

declare -A pkg_map_zypper=(
  [meson]="meson"
  [ninja]="ninja"
  [glslc]="shaderc"
  [vulkaninfo]="vulkan-tools"
  [pkg-config]="pkg-config"
  [sdl3-dev]="libSDL3-devel"
)

# Add more mappings as needed...

# --- Map missing dependencies to package names for the selected package manager ---
map_packages() {
  local mgr=$1
  shift
  local deps=("$@")
  local pkgs=()

  local map_var="pkg_map_${mgr}"
  if ! declare -p "$map_var" &>/dev/null; then
    echo "Warning: No package mapping for package manager '$mgr'. Using dependency names."
    pkgs=("${deps[@]}")
  else
    declare -n pkg_map="$map_var"
    for dep in "${deps[@]}"; do
      if [[ -n "${pkg_map[$dep]:-}" ]]; then
        pkgs+=("${pkg_map[$dep]}")
      else
        echo "Warning: No package mapping for dependency '$dep' with package manager '$mgr'. Using dependency name."
        pkgs+=("$dep")
      fi
    done
  fi

  echo "${pkgs[@]}"
}

# --- Check if a command exists ---
check_command() {
  command -v "$1" &>/dev/null
}

# --- Check if SDL3 development files are installed ---
check_sdl3_dev() {
  pkg-config --exists sdl3
}

# --- Check all dependencies and return missing ones ---
check_dependencies() {
  local missing=()
  local cmd

  # Commands/tools required to build and run
  local required_cmds=(meson ninja glslc vulkaninfo pkg-config)

  for cmd in "${required_cmds[@]}"; do
    if ! check_command "$cmd"; then
      missing+=("$cmd")
    fi
  done

  if ! check_sdl3_dev; then
    missing+=("sdl3-dev")
  fi

  echo "${missing[@]}"
}

# --- Main script logic ---
main() {
  # Detect installed package managers
  mapfile -t installed_mgrs < <(detect_installed_package_managers)

  # Prompt user to choose package manager
  pkg_mgr=$(choose_package_manager "${installed_mgrs[@]}")
  echo "Using package manager: $pkg_mgr"

  local missing
  IFS=' ' read -r -a missing <<< "$(check_dependencies)"

  if ((${#missing[@]})); then
    echo "Missing dependencies: ${missing[*]}"

    local install_cmd=${install_cmd_map[$pkg_mgr]}
    if [[ -z "$install_cmd" ]]; then
      echo "Unsupported package manager command for '$pkg_mgr'."
      echo "Please install dependencies manually."
      exit 1
    fi

    read -rp "Install missing packages? [Y/n] " ans
    ans=${ans:-Y}

    if [[ $ans =~ ^[Yy]$ ]]; then
      local packages
      IFS=' ' read -r -a packages <<< "$(map_packages "$pkg_mgr" "${missing[@]}")"

      echo "Installing packages: ${packages[*]}"
      if [[ "$(id -u)" -ne 0 ]]; then
        sudo "$pkg_mgr" $install_cmd "${packages[@]}"
      else
        "$pkg_mgr" $install_cmd "${packages[@]}"
      fi
    else
      echo "Please install missing dependencies manually and re-run the script."
      exit 1
    fi
  else
    echo "All dependencies are satisfied."
  fi

  # Set environment variables for runtime
  : "${XDG_RUNTIME_DIR:=/run/user/$(id -u)}"
  export XDG_RUNTIME_DIR
  echo "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"

  if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    export SDL_VIDEODRIVER=wayland
  else
    export SDL_VIDEODRIVER=x11
  fi
  echo "SDL_VIDEODRIVER=$SDL_VIDEODRIVER"

  # Build project
  echo "Setting up Meson build directory..."
  meson setup build --wipe || meson setup build

  echo "Building project with Ninja..."
  ninja -C build

  # Run demo
  echo "Running Vulkan+SDL3 triangle demo..."
  (cd build && ./triangle)
}

main "$@"
