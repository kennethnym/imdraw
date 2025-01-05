#!/bin/bash

set -eu

# make sure the cwd is where the script is.
pushd "$(dirname "$0")" >> /dev/null

CC=gcc
AR=ar

src=(
	src/main.c
)

cimgui_flags="-L./vendor/cimgui -lcimgui -I./vendor/cimgui"
sokol_flags="-L./build -lsokol -I./vendor/sokol -I./vendor/sokol/util"
compile_flags="-o ./build/imdraw -framework QuartzCore -framework Cocoa -framework MetalKit -framework Metal"

mkdir -p build/

# compile cimgui into a static library
if [ ! -f vendor/cimgui/libcimgui.a ]; then
	echo "Compiling cimgui..."
	pushd vendor/cimgui >> /dev/null
	make static
	popd >> /dev/null
	echo "cimgui compiled!"
else
	echo "libcimgui exists at vendor/cimgui, skipping compilation!"
fi

# compile sokol into a static library
if [ ! -f build/libsokol.a ]; then
	echo "Compiling Sokol..."
	$CC -c -x objective-c lib/sokol.c -o build/sokol.o $cimgui_flags $sokol_flags
	$AR rcs build/libsokol.a build/sokol.o
	rm build/sokol.o
	echo "Sokol compiled!"
else
	echo "libsokol.a exists, skipping compilation!"
fi

compile_cmd="$CC $src $compile_flags $cimgui_flags $sokol_flags"

echo $compile_cmd
$compile_cmd

popd >> /dev/null

