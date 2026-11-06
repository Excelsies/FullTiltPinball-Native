#!/usr/bin/env bash
# Cross-compiles the 64-bit Windows build with MinGW-w64 on Linux.
#
#   Platform/Windows/build-mingw.sh [deps|build|all]     (default: all)
#
# deps:  downloads the pinned SDL2 / SDL2_mixer MinGW development releases into Libs/downloads/,
#        checks their SHA-256 and unpacks the x86_64 parts into Libs/SDL2 and Libs/SDL2_mixer.
# build: configures build-win/ with mingwcc.cmake, builds Release into bin-win/ and checks that
#        every DLL the executable and its DLLs import is either in bin-win/ or part of Windows.
# Libs/, build-win/ and bin-win/ are git-ignored. Re-running skips what is already in place.

set -euo pipefail

sdl_version='2.32.10'
sdl_sha256='83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2'
mixer_version='2.8.2'
mixer_sha256='6872029bcca09b12985750d38de8865c66fbef5567715f0e4d89533ccaf3a0f2'
triplet='x86_64-w64-mingw32'

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
libs="$repo/Libs"
build_dir="$repo/build-win"
out_dir="$repo/bin-win"

die() { echo "build-mingw: $*" >&2; exit 1; }

need() {
	local tool
	for tool in "$@"; do
		command -v "$tool" >/dev/null || die "'$tool' not found"
	done
}

# fetch_unpack <name> <github repo> <version> <sha256> <archive stem>
fetch_unpack() {
	local name="$1" project="$2" version="$3" sha="$4" stem="$5"
	local file="$stem-devel-$version-mingw.tar.gz"
	local url="https://github.com/libsdl-org/$project/releases/download/release-$version/$file"
	local dest="$libs/$name" stamp="$libs/$name/.release"

	if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$file $sha" ]; then
		echo "$name $version already unpacked"
		return
	fi

	mkdir -p "$libs/downloads"
	if [ ! -f "$libs/downloads/$file" ]; then
		echo "downloading $url"
		curl -sSfL -o "$libs/downloads/$file.part" "$url" || die "download failed: $url"
		mv "$libs/downloads/$file.part" "$libs/downloads/$file"
	fi
	if ! echo "$sha  $libs/downloads/$file" | sha256sum -c --quiet -; then
		rm -f "$libs/downloads/$file"
		die "SHA-256 mismatch for $file (deleted; run again to download it anew)"
	fi

	local tmp
	tmp="$(mktemp -d "$libs/unpack.XXXXXX")"
	tar -xzf "$libs/downloads/$file" -C "$tmp" --no-same-owner --no-same-permissions \
		|| { rm -rf "$tmp"; die "cannot unpack $file"; }
	local src="$tmp/$stem-$version/$triplet"
	[ -d "$src/include/SDL2" ] && [ -d "$src/lib" ] && [ -d "$src/bin" ] \
		|| { rm -rf "$tmp"; die "$file has no $triplet/{include/SDL2,lib,bin}"; }
	rm -rf "$dest"
	mv "$src" "$dest"
	rm -rf "$tmp"
	echo "$file $sha" > "$stamp"
	echo "$name $version unpacked into $dest"
}

deps() {
	need curl sha256sum tar
	fetch_unpack SDL2 SDL "$sdl_version" "$sdl_sha256" SDL2
	fetch_unpack SDL2_mixer SDL_mixer "$mixer_version" "$mixer_sha256" SDL2_mixer
}

# DLLs every 64-bit Windows provides; anything else must be shipped in bin-win/.
system_dll() {
	case "${1,,}" in
		kernel32.dll|user32.dll|gdi32.dll|winmm.dll|imm32.dll|ole32.dll|oleaut32.dll|shell32.dll| \
		version.dll|advapi32.dll|setupapi.dll|msvcrt.dll|ucrtbase.dll|api-ms-win-*.dll|cfgmgr32.dll| \
		comdlg32.dll|dinput8.dll|dxgi.dll|hid.dll|rpcrt4.dll|shlwapi.dll|uuid.dll|winspool.drv| \
		ntdll.dll|bcrypt.dll|ws2_32.dll|dwmapi.dll|uxtheme.dll)
			return 0 ;;
	esac
	return 1
}

check_imports() {
	local objdump="$triplet-objdump" bad=0 file dll
	need "$objdump"
	for file in "$out_dir"/*.exe "$out_dir"/*.dll; do
		[ -f "$file" ] || die "missing $file"
		while read -r dll; do
			if [ -f "$out_dir/$dll" ]; then
				echo "  $(basename "$file") -> $dll (shipped)"
			elif system_dll "$dll"; then
				echo "  $(basename "$file") -> $dll (Windows)"
			else
				echo "  $(basename "$file") -> $dll MISSING" >&2
				bad=1
			fi
		done < <("$objdump" -p "$file" | sed -n 's/^[[:space:]]*DLL Name: //p')
	done
	[ "$bad" -eq 0 ] || die "imports not satisfied (see MISSING above)"
}

build() {
	need cmake "$triplet-gcc"
	[ -f "$libs/SDL2/.release" ] && [ -f "$libs/SDL2_mixer/.release" ] \
		|| die "SDL libraries missing; run '$0 deps' first"
	cmake -S "$repo" -B "$build_dir" -DCMAKE_TOOLCHAIN_FILE="$repo/mingwcc.cmake" \
		-DCMAKE_BUILD_TYPE=Release -DFT_OUTPUT_DIR="$out_dir" || die "configure failed"
	cmake --build "$build_dir" -j "$(nproc)" || die "build failed"
	[ -f "$out_dir/SpaceCadetPinball.exe" ] || die "no $out_dir/SpaceCadetPinball.exe"
	echo "imports:"
	check_imports
	echo "built $out_dir/SpaceCadetPinball.exe"
}

case "${1:-all}" in
	deps) deps ;;
	build) build ;;
	all) deps; build ;;
	*) die "usage: $0 [deps|build|all]" ;;
esac
