#!/bin/bash

set -e

echo "compile core"
export YC_CCACHE=true
./acore.sh "compiler" "all"

