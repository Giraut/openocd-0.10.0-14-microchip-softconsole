#!/bin/bash

set -euo pipefail
IFS=$'\n\t'

DIR=`pwd`
BASE=`basename $DIR`
TS=`date +"%Y%m%d-%H%M%S"`

echo $DIR
echo $BASE

cd ..
zip -9 -r $BASE-src_and_build_scripts-$TS.zip $BASE/build_scripts $BASE/openocd-softconsole-src $BASE/README.md $BASE/.gitignore $BASE/pack.sh $BASE/build.sh
cd $BASE