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

ifeq ($(CASE),router)
TARGET := $(TARGET):topology=router
else ifeq ($(CASE),mesh)
TARGET := $(TARGET):topology=mesh
else ifeq ($(CASE),floogen_xy)
TARGET := $(TARGET):topology=floogen_xy
else ifeq ($(CASE),floogen_id)
TARGET := $(TARGET):topology=floogen_id
else ifneq ($(filter graph_%,$(CASE)),)
TARGET := $(TARGET):topology=$(CASE)
endif

include $(GVSOC_ROOT)/gvsoc/core/tests/common.mk
