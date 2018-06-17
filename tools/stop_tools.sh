#!/bin/bash

kbase_version=1.0.0

tool_path=$(dirname $(readlink -f "$0"))

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$tool_path/../depends/kbase/$kbase_version/lib/

cd $tool_path/registry
./registry -stop
