# Define the build directory so the run command knows where to look
BUILDDIR ?= build

# This target builds the GVSOC simulator with your custom module included
gvsoc:
	PYTHONPATH=/home/s/FlooNoC:$$PYTHONPATH make -C ../../.. TARGETS=magia_flex MODULES=$(CURDIR) build

# This target runs the simulation
run:
	gvsoc --target-dir=$(CURDIR) --target=magia_flex --work-dir=$(BUILDDIR) --binary=$(BUILDDIR)/test run $(runner_args)

# A convenience target so you can type 'make all'
all: gvsoc