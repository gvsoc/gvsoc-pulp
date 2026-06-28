#
# Copyright (C) 2026 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
GVSOC_ROOT ?= ../../../..
TARGET = test
CASE ?= default

ifeq ($(CASE),mem.4)
TARGET := $(TARGET):use_memory=true,mem_bw=4
else ifeq ($(CASE),mem.8)
TARGET := $(TARGET):use_memory=true,mem_bw=8
else ifeq ($(CASE),mem.64)
TARGET := $(TARGET):use_memory=true,mem_bw=64
endif

include $(GVSOC_ROOT)/gvsoc/core/tests/common.mk
