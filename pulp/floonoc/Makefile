BUILDDIR ?= build

gvsoc:
	make -C ../../.. TARGETS=magia MODULES=$(CURDIR) build

run:
	/home/s/gvsoc/install/bin/gvsoc --target-dir=$(CURDIR) --target=magia --work-dir=$(BUILDDIR) --binary=/home/s/gvsoc/examples/floonoc.elf run