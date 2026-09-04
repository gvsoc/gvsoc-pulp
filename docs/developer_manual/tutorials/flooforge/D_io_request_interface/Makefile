include ../utils/rt.mk

GVSOC_ROOT ?= ../../../../../..
GVSOC_WORKDIR ?= $(abspath $(GVSOC_ROOT))/build_flooforge/$(notdir $(CURDIR))
ifdef BUILDDIR
FULL_BUILDDIR := $(GVSOC_WORKDIR)/$(BUILDDIR)
else
FULL_BUILDDIR := $(GVSOC_WORKDIR)
endif

clean:
	rm -rf __pycache__
	rm -rf $(FULL_BUILDDIR)/test $(FULL_BUILDDIR)/work

all:
	mkdir -p $(FULL_BUILDDIR)/test
	riscv64-unknown-elf-gcc -o $(FULL_BUILDDIR)/test/test main.c $(RT_SRCS) $(RT_FLAGS)

gvsoc:
	make -C $(GVSOC_ROOT) TARGETS=my_system MODULES=$(CURDIR) BUILDDIR=$(FULL_BUILDDIR)/build INSTALLDIR=$(FULL_BUILDDIR)/install $(OPTIONS) build

prepare:
	cp solution/* .

run:
	$(FULL_BUILDDIR)/install/bin/gvrun --target-dir=$(CURDIR) --target=my_system --work-dir=$(FULL_BUILDDIR)/work --parameter binary=$(FULL_BUILDDIR)/test/test run $(runner_args)
