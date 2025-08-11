#pragma once
#include "archi_redmule.h"

#define SRC_FMT FP16
typedef _Float16 src_fmt_t;

#define DST_FMT FP32
typedef float dst_fmt_t;

#define ARRAY_HEIGHT 4
#define PIPE_REGS    3
#define ARRAY_WIDTH  (PIPE_REGS * ARRAY_HEIGHT)

#define DATA_WIDTH 256