WORK_DIR ?= work
USE_GVRUN = 1

clean:
	make -C ../../../../.. TARGETS=test MODULES=$(CURDIR) clean

build:
	make -C ../../../../.. TARGETS=test MODULES=$(CURDIR) build

all: build

run: $(WORK_DIR)
	gvsoc --target-dir=$(CURDIR) --target=test --build-dir=$(WORK_DIR) build run $(runner_args)

$(WORK_DIR):
	mkdir -p $(WORK_DIR)

.PHONY: build
