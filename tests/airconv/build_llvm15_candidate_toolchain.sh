#!/usr/bin/env bash
# Experimental LLVM 15 candidate: build into a new isolated directory only.
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  printf 'Usage: %s LLVM_SOURCE NEW_OUTPUT_DIRECTORY [x86_64|arm64]\n' "$0" >&2
  exit 2
fi
arch=${3:-x86_64}
if [[ "$arch" != x86_64 && "$arch" != arm64 ]]; then
  printf 'Unsupported architecture: %s\n' "$arch" >&2
  exit 2
fi
repo_dir=$(cd "$(dirname "$0")/../.." && pwd -P)
source_dir=$(cd "$1" && pwd -P)
output_dir=$2
if [[ -e "$output_dir" ]]; then
  printf 'Output directory already exists: %s\n' "$output_dir" >&2
  exit 2
fi
if [[ $(git -C "$source_dir" describe --tags --exact-match) != llvmorg-15.0.7 ]]; then
  printf 'Expected llvmorg-15.0.7 source checkout\n' >&2
  exit 2
fi
mkdir "$output_dir"
output_dir=$(cd "$output_dir" && pwd -P)
git clone --shared --no-hardlinks --branch llvmorg-15.0.7 "$source_dir" "$output_dir/source"
for patch_file in experimental-llvm15-empty-ptrauth.patch experimental-llvm15-placeholder-cleanup.patch; do
  patch --batch -p1 -d "$output_dir/source" < "$repo_dir/patches/llvm/$patch_file"
done
cmake -B "$output_dir/build" -S "$output_dir/source/llvm" \
  -DCMAKE_INSTALL_PREFIX="$output_dir/install" \
  -DCMAKE_OSX_ARCHITECTURES="$arch" \
  -DLLVM_HOST_TRIPLE="$arch-apple-darwin" \
  -DLLVM_ENABLE_ASSERTIONS=On -DLLVM_ENABLE_ZSTD=Off \
  -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD= -DLLVM_BUILD_TOOLS=Off \
  -DBUG_REPORT_URL=https://github.com/3Shain/dxmt -DPACKAGE_VENDOR=DXMT \
  -DLLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off -G Ninja
cmake --build "$output_dir/build" --parallel "${DXMT_LLVM_BUILD_JOBS:-8}"
cmake --install "$output_dir/build"
printf 'Experimental LLVM candidate installed to %s/install\n' "$output_dir"
