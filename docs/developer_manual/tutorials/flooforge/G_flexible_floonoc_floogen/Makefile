GVSOC_ROOT ?= ../../../../../..
GVSOC_WORKDIR ?= $(CURDIR)/build
ifdef BUILDDIR
FULL_BUILDDIR := $(GVSOC_WORKDIR)/$(BUILDDIR)
else
FULL_BUILDDIR := $(GVSOC_WORKDIR)
endif

topology:
	python3 gen_topology.py --dim-x 2 --dim-y 2 --dim-z 2 \
		--link-latency 1 --z-link-latency 8 --out-dir generated --name mesh3d

plot: topology
	python3 plot_latency.py --dir generated --name mesh3d

clean:
	rm -rf generated
	make -C $(GVSOC_ROOT) TARGETS=my_system MODULES=$(CURDIR) BUILDDIR=$(FULL_BUILDDIR)/build INSTALLDIR=$(FULL_BUILDDIR)/install $(OPTIONS) clean

gvsoc: topology
	make -C $(GVSOC_ROOT) TARGETS=my_system MODULES=$(CURDIR) BUILDDIR=$(FULL_BUILDDIR)/build INSTALLDIR=$(FULL_BUILDDIR)/install $(OPTIONS) build

run:
	mkdir -p $(FULL_BUILDDIR)/work
	$(FULL_BUILDDIR)/install/bin/gvrun --target-dir=$(CURDIR) --target=my_system --work-dir=$(FULL_BUILDDIR)/work run $(runner_args)

.PHONY: topology plot clean gvsoc run
