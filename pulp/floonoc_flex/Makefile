BUILDDIR ?= build

gvsoc:
	PYTHONPATH=/home/s/FlooNoC:$$PYTHONPATH make -C ../../.. TARGETS=magia_flex MODULES=$(CURDIR) build

run:
	PYTHONPATH=/home/s/FlooNoC:$$PYTHONPATH /home/s/gvsoc/install/bin/gvsoc --target-dir=$(CURDIR) --target=magia_flex --work-dir=$(BUILDDIR) --binary=/home/s/gvsoc/examples/floonoc.elf run --trace=insn 
#2>&1 | tee log.txt