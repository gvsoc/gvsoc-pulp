include ../utils/rt.mk

GVSOC_ROOT ?= ../../../../../..
GVSOC_WORKDIR ?= $(CURDIR)/build
ifdef BUILDDIR
FULL_BUILDDIR := $(GVSOC_WORKDIR)/$(BUILDDIR)
else
FULL_BUILDDIR := $(GVSOC_WORKDIR)
endif

clean:
	rm -rf $(FULL_BUILDDIR)/test
	make -C $(GVSOC_ROOT) TARGETS=my_system MODULES=$(CURDIR) BUILDDIR=$(FULL_BUILDDIR)/build INSTALLDIR=$(FULL_BUILDDIR)/install $(OPTIONS) clean

all:
	mkdir -p $(FULL_BUILDDIR)/test
	riscv64-unknown-elf-gcc -g -o $(FULL_BUILDDIR)/test/test main.c $(RT_SRCS) $(RT_FLAGS)

gvsoc:
	make -C $(GVSOC_ROOT) TARGETS=my_system MODULES=$(CURDIR) BUILDDIR=$(FULL_BUILDDIR)/build INSTALLDIR=$(FULL_BUILDDIR)/install $(OPTIONS) build

prepare_traces:
	cp solution_traces/* .

prepare_vcd:
	cp solution_vcd/* .

run:
	$(FULL_BUILDDIR)/install/bin/gvrun --target-dir=$(CURDIR) --target=my_system --work-dir=$(FULL_BUILDDIR)/work --parameter binary=$(FULL_BUILDDIR)/test/test run $(runner_args)
