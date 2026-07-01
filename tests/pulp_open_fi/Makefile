GVSOC_TOP ?= $(abspath ../../..)
GVSOC_WORKDIR ?= $(GVSOC_TOP)/build/pulp-open_fi

ifdef BUILDDIR
FULL_BUILDDIR ?= $(GVSOC_WORKDIR)/$(BUILDDIR)
else
FULL_BUILDDIR ?= $(GVSOC_WORKDIR)
endif

BINARY ?= $(CURDIR)/binary

.PHONY: clean gvsoc test

clean:
	rm -rf $(GVSOC_WORKDIR)

gvsoc:
	$(MAKE) -C $(GVSOC_TOP) \
		TARGETS=pulp-open:chip/soc/fic=true \
		BUILDDIR=$(FULL_BUILDDIR)/build \
		INSTALLDIR=$(FULL_BUILDDIR)/install \
		$(OPTIONS) build

test: gvsoc
	$(FULL_BUILDDIR)/install/bin/gvsoc \
		--target=pulp-open:chip/soc/fic=true \
		--work-dir=$(FULL_BUILDDIR)/work \
		--binary=$(BINARY) \
		--config-opt=chip/soc/fic/faults_path=$(CURDIR)/faults \
		image flash run $(runner_args)

fcm: gvsoc
	PYTHONPATH=$(FULL_BUILDDIR)/install/python:$$PYTHONPATH \
		python3 ./fcm.py \
		--builddir $(FULL_BUILDDIR) \
		--binary $(BINARY) \
		--target pulp-open:chip/soc/fic=true
