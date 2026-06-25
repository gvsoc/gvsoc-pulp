# GVSoC repository root (five directories up from this Makefile)
GVSOC_ROOT := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST))))../../../../../)

include helpers.mk
include tasks.mk
include solutions.mk

.PHONY: build
build:
	$(MAKE) -C $(GVSOC_ROOT) build
