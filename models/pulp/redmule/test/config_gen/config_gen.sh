#!/bin/bash

CONFIG_PATH="$1/inc/config.h"

echo Gerating configuration file for test... 

exec > $CONFIG_PATH

echo "#define ARRAY_HEIGHT $2" 
echo "#define PIPE_REGS    $3"
echo "#define ARRAY_WIDTH  (PIPE_REGS * ARRAY_HEIGHT)"

echo

case $4 in
    FP8)
    echo "#define DATA_WIDTH $((8 * ($3 + 1) * $2))"
    ;;

    FP16)
    echo "#define DATA_WIDTH $((16 * ($3 + 1) * $2))"
    ;;

    FP32)
    echo "#define DATA_WIDTH $((32 * ($3 + 1) * $2))"
    ;;

    *)
    exit 1
    ;;
esac
