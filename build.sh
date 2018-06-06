#!/bin/bash -xv

proj_path=$(cd `dirname $0`; pwd)
build_dir=cmake-build-debug

mkdir -p $build_dir
cd $proj_path/$build_dir
rm -f CMakeCache.txt
cmake ..
make install
