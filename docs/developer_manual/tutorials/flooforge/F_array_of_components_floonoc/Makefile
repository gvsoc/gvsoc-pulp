GVSOC_ROOT ?= ../../../../../..
GVSOC_WORKDIR ?= $(abspath $(GVSOC_ROOT))/build_flooforge/$(notdir $(CURDIR))
ifdef BUILDDIR
FULL_BUILDDIR := $(GVSOC_WORKDIR)/$(BUILDDIR)
else
FULL_BUILDDIR := $(GVSOC_WORKDIR)
endif

clean:
	rm -rf __pycache__
	rm -rf $(FULL_BUILDDIR)/work

gvsoc:
	make -C $(GVSOC_ROOT) TARGETS=my_system MODULES=$(CURDIR) BUILDDIR=$(FULL_BUILDDIR)/build INSTALLDIR=$(FULL_BUILDDIR)/install $(OPTIONS) build

prepare:
	cp solution/* .

run:
	mkdir -p $(FULL_BUILDDIR)/work
	$(FULL_BUILDDIR)/install/bin/gvrun --target-dir=$(CURDIR) --target=my_system --work-dir=$(FULL_BUILDDIR)/work run $(runner_args)

.PHONY: clean gvsoc prepare run
