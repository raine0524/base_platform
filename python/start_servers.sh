#!/bin/bash

root_dir=$(dirname $(readlink -f "$0"))

/usr/bin/python ${root_dir}/plot/server.py >/dev/null 2>&1 &