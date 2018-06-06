#!/bin/bash

tool_path=$(dirname $(readlink -f "$0"))

cd $tool_path/registry
./registry -start
