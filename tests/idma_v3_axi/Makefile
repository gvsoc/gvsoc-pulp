GVSOC_ROOT ?= ../../../..
TARGET = test
CASE ?= 1d_4k_aligned
TARGET := $(TARGET):case=$(CASE)
# The tester component is shared with the cluster testbench
MODULES = $(CURDIR);$(CURDIR)/../idma_v3_cluster

include $(GVSOC_ROOT)/gvsoc/core/tests/common.mk
