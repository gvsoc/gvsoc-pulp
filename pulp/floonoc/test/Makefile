WORK_DIR ?= work

clean:
	make -C ../../../.. TARGETS=test MODULES=$(CURDIR) clean

build:
	make -C ../../../.. TARGETS=test MODULES=$(CURDIR) build

all: build

run: $(WORK_DIR)
	gvsoc --target-dir=$(CURDIR) --target=test --work-dir=$(WORK_DIR) run $(runner_args)

$(WORK_DIR):
	mkdir -p $(WORK_DIR)

.PHONY: build
