#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:${PATH}"
export PKG_CONFIG_PATH="/c/msys64/ucrt64/lib/pkgconfig"

mkdir -p "$ROOT/build"
"$ROOT/../.venv/Scripts/python.exe" "$ROOT/../packaging/make_icon.py"
printf '1 ICON "app.ico"\n1 24 "app.manifest"\n' > "$ROOT/build/app.rc"
cp "$ROOT/app.manifest" "$ROOT/build/app.manifest"
windres -O coff -i "$ROOT/build/app.rc" -o "$ROOT/build/app.res.o"

g++ -std=c++17 -O2 -Wall -Wextra -mwindows -municode \
  -finput-charset=UTF-8 -fexec-charset=UTF-8 \
  -I"$ROOT/third_party/onnxruntime-win-x64-1.29.0/include" \
  $(pkg-config --cflags opencv4) \
  "$ROOT/src/main.cpp" \
  "$ROOT/src/vision.cpp" \
  "$ROOT/src/capture.cpp" \
  "$ROOT/src/overlay.cpp" \
  "$ROOT/src/speech.cpp" \
  "$ROOT/src/onnx_models.cpp" \
  "$ROOT/src/settings.cpp" \
  "$ROOT/build/app.res.o" \
  -lopencv_imgcodecs \
  $(pkg-config --libs opencv4) \
  -lgdi32 -luser32 -luxtheme -lole32 -luuid -loleaut32 -lcomctl32 -lcomdlg32 \
  -o "$ROOT/build/PUBG-Mortar-Calculator.exe"

copy_deps() {
  local file="$1"
  local line path base src
  while read -r line; do
    path="$(sed -n 's/.*=> \([^ ]*\).*/\1/p' <<<"$line")"
    [[ -z "$path" ]] && continue
    base="$(basename "$path")"
    [[ -f "$ROOT/build/$base" ]] && continue
    src=""
    if [[ -f "$path" ]]; then
      src="$path"
    elif [[ -f "/c/msys64/ucrt64/bin/$base" ]]; then
      src="/c/msys64/ucrt64/bin/$base"
    elif [[ -f "/c/msys64/mingw64/bin/$base" ]]; then
      src="/c/msys64/mingw64/bin/$base"
    elif [[ -f "/mingw64/bin/$base" ]]; then
      src="/mingw64/bin/$base"
    fi
    case "$src" in
      ""|/c/WINDOWS/*|/c/Windows/*|/c/windows/*) continue ;;
    esac
    cp "$src" "$ROOT/build/$base"
    copy_deps "$ROOT/build/$base"
  done < <(ldd "$file" || true)
}

copy_deps "$ROOT/build/PUBG-Mortar-Calculator.exe"

VENV="$ROOT/../.venv/Lib/site-packages/onnxruntime/capi"
if [[ -f "$VENV/onnxruntime.dll" ]]; then
  cp "$VENV/onnxruntime.dll" "$ROOT/build/"
  cp "$VENV/onnxruntime_providers_shared.dll" "$ROOT/build/"
else
  echo "warning: onnxruntime.dll was not found in the Python environment" >&2
fi

echo "built $ROOT/build/PUBG-Mortar-Calculator.exe"
