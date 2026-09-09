GVSOC_ROOT ?= ../../../../../..
GVSOC_WORKDIR ?= $(abspath $(GVSOC_ROOT))/build_flooforge/$(notdir $(CURDIR))
ifdef BUILDDIR
FULL_BUILDDIR := $(GVSOC_WORKDIR)/$(BUILDDIR)
else
FULL_BUILDDIR := $(GVSOC_WORKDIR)
endif

DIM_X ?= 3
DIM_Y ?= 3
DIM_Z ?= 2
LINK_LATENCY ?= 1
Z_LINK_LATENCY ?= 8

topology:
	python3 gen_topology.py --dim-x $(DIM_X) --dim-y $(DIM_Y) --dim-z $(DIM_Z) \
		--link-latency $(LINK_LATENCY) --z-link-latency $(Z_LINK_LATENCY) --out-dir generated --name mesh3d

plot: topology
	python3 plot_latency.py --dir generated --name mesh3d

clean:
	rm -rf __pycache__
	rm -rf generated
	rm -rf $(FULL_BUILDDIR)/work

gvsoc: topology
	make -C $(GVSOC_ROOT) TARGETS=my_system MODULES=$(CURDIR) BUILDDIR=$(FULL_BUILDDIR)/build INSTALLDIR=$(FULL_BUILDDIR)/install $(OPTIONS) build

prepare:
	cp solution/* .

run:
	mkdir -p $(FULL_BUILDDIR)/work
	$(FULL_BUILDDIR)/install/bin/gvrun --target-dir=$(CURDIR) --target=my_system --work-dir=$(FULL_BUILDDIR)/work run $(runner_args)

Z_LATENCIES ?= 1,2,4,8

sweep:
	python3 sweep_z_latency.py --z-latencies $(Z_LATENCIES) \
		--dim-x $(DIM_X) --dim-y $(DIM_Y) --dim-z $(DIM_Z) --link-latency $(LINK_LATENCY)

.PHONY: topology plot clean gvsoc prepare run sweep
