#!/usr/bin/env bash
# Configures and builds with MSVC and Ninja:
#   .github/scripts/msvc-build.sh <build dir> [-Dname=value ...]
# vcvars puts cl and Ninja on PATH; vswhere finds it on a runner and on a machine with only
# the Build Tools. vcvars is a batch file, so cmd runs it once and its environment is taken
# over here. /MT makes the exe a single file needing no redistributable.
set -euo pipefail
cd "$(dirname "$0")/../.."

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <build dir> [-Dname=value ...]" >&2
  exit 2
fi
build=$1
shift

vswhere="$(cygpath -F 42)/Microsoft Visual Studio/Installer/vswhere.exe"  # Program Files (x86)
vsroot=$("$vswhere" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | tr -d '\r')
if [[ -z $vsroot ]]; then
  echo 'No Visual Studio with the C++ tools was found.' >&2
  exit 1
fi

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
# A batch file, as cmd does not understand the quoting bash would give a command line.
printf '@chcp 65001 >nul\r\n@call "%s\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul || exit /b 1\r\n@set\r\n' "$vsroot" > "$scratch/env.bat"
cmd //c "$(cygpath -w "$scratch/env.bat")" | tr -d '\r' > "$scratch/env.txt"
while IFS='=' read -r name value; do
  case $name in
    PATH | Path) PATH="$(cygpath -u -p "$value"):$PATH" ;;
    INCLUDE | LIB | LIBPATH | VS* | VC* | Windows* | UCRT* | UniversalCRT* | Framework* | DevEnvDir | ExtensionSdkDir | Platform)
      [[ $name =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] && export "$name=$value" ;;
  esac
done < "$scratch/env.txt"
export PATH
command -v cl >/dev/null || { echo 'vcvars did not put cl on PATH' >&2; exit 1; }

cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl \
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded "$@"
cmake --build "$build"
