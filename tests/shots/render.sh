#!/usr/bin/env bash
# Renders the scenes in scenes.txt on a hidden Win32 desktop, into a folder of PNGs. A
# refactoring renders before and after, then compare.sh proves that nothing moved. Images
# depend on the machine (fonts, driver), so they are not committed.
#
#   tests/shots/render.sh --exe build-msvc/apps/gui/crpp.exe --out <folder>
#       [--size 951x692|auto] [--hour 14] [--dpi 144] [--only home,chat] [-- <more options>]
#
# What follows -- goes to every scene, for example: -- --theme luna_blue --locale en.
# crpp_hidden_run beside the exe puts each run on the hidden desktop.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)

exe='' out='' size='951x692' hour=14 dpi=96 only=''
extra=()
while [[ $# -gt 0 ]]; do
  case $1 in
    --exe) exe=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --size) size=$2; shift 2 ;;
    --hour) hour=$2; shift 2 ;;
    --dpi) dpi=$2; shift 2 ;;
    --only) only=",$2,"; shift 2 ;;
    --) shift; extra=("$@"); break ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[[ -n $exe && -n $out ]] || { echo "usage: $0 --exe <crpp.exe> --out <folder> [...]" >&2; exit 2; }

runner="$(dirname "$exe")/crpp_hidden_run.exe"
[[ -x $runner ]] || { echo "no crpp_hidden_run.exe beside $exe" >&2; exit 2; }
mkdir -p "$out/.data"
out_w=$(cygpath -w "$(cd "$out" && pwd)")

failed=0
while IFS= read -r line || [[ -n $line ]]; do
  line=${line%$'\r'}
  [[ -z $line || $line == \#* ]] && continue
  read -ra words <<< "$line"
  [[ -n $only && $only != *",${words[0]},"* ]] && continue
  png="$out_w\\${words[0]}.png"
  # Options are read from the end of the command line; --data must stay last.
  options=()
  [[ $dpi != 96 ]] && options+=(--dpi "$dpi")
  options+=("${extra[@]}")
  code=0
  "$runner" 30 "$exe" --shot "$png" "$size" "$hour" "${words[@]}" "${options[@]}" --data "$out_w\\.data" || code=$?
  # Either of these would be the same for every scene left, so going on only costs time.
  if [[ $code == 124 ]]; then echo "$line did not finish in 30 s" >&2; exit 1; fi
  if [[ $code == 3 ]]; then echo "$line could not start: $(cat "$out/${words[0]}.png.error" 2>/dev/null)" >&2; exit 1; fi
  if [[ $code != 0 ]]; then echo "FAILED $line -> $code"; failed=$((failed + 1)); fi
done < "$here/scenes.txt"
echo "rendered into $out, $failed failed"
[[ $failed == 0 ]]
