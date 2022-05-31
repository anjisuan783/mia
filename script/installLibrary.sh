#!/bin/bash

buildLibrary() {
  if [ -d $BUILD_DIR ]; then
    cd $BUILD_DIR
    for d in */ ; do
      if [ "$d" != "libdeps/" ];
      then
        cd $d
          ninja
          if [[ "$?" -ne 0 ]];then
            exit 1
          fi
        cd ..
      fi
    done
    cd $ROOT_DIR
  else
    echo "Error, build directory does not exist, run generateProject.sh first"
  fi
}

generateVersion() {
  echo "generating $1"
  cmake ./rtc_stack  -B$BUILD_DIR/wa -G "Ninja" "-DWA_BUILD_TYPE=$1" -Wno-dev
  cmake ./src  -B$BUILD_DIR/ma -G "Ninja" "-DMA_BUILD_TYPE=$1" -Wno-dev
}

