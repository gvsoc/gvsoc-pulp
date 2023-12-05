#!/bin/bash

CONFIG_PATH="$1/include/config.h"

echo Gerating configuration file for model... 

exec > $CONFIG_PATH

echo "#include \"archi_redmule.h\""
echo

case $2 in
    FP8)
    echo "#define SRC_FMT FP8"
    echo "typedef uint8_t src_fmt_t;"
    ;;

    FP16)
    echo "#define SRC_FMT FP16"
    echo "typedef _Float16 src_fmt_t;"
    ;;

    FP32)
    echo "#define SRC_FMT FP32"
    echo "typedef float src_fmt_t;"
    ;;

    *)
    exit 1
    ;;
esac

echo

case $3 in
    FP8)
    echo "#define DST_FMT FP8"
    echo "typedef uint8_t dst_fmt_t;"
    ;;

    FP16)
    echo "#define DST_FMT FP16"
    echo "typedef _Float16 dst_fmt_t;"
    ;;

    FP32)
    echo "#define DST_FMT FP32"
    echo "typedef float dst_fmt_t;"
    ;;

    *)
    exit 1
    ;;
esac

echo

echo "#define ARRAY_HEIGHT $4" 
echo "#define PIPE_REGS    $5"
echo "#define ARRAY_WIDTH  (PIPE_REGS * ARRAY_HEIGHT)"

echo

case $2 in
    FP8)
    echo "#define DATA_WIDTH $((8 * ($5 + 1) * $4))"
    ;;

    FP16)
    echo "#define DATA_WIDTH $((16 * ($5 + 1) * $4))"
    ;;

    FP32)
    echo "#define DATA_WIDTH $((32 * ($5 + 1) * $4))"
    ;;

    *)
    exit 1
    ;;
esac

echo