#!/bin/bash

set -euo pipefail
IFS=$'\n\t'

bash ./build_scripts/scripts/build.sh --linux64 --win64
