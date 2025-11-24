WORK_DIR ?= work

GVSOC_BIN ?= ../../../../install/bin/gvsoc

clean:
	make -C ../../../.. TARGETS=test MODULES=$(CURDIR) clean

build:
	make -C ../../../.. TARGETS=test MODULES=$(CURDIR) build

all: build

run: $(WORK_DIR)
	$(GVSOC_BIN) --target-dir=$(CURDIR) --target=test --work-dir=$(WORK_DIR) run $(runner_args) --trace-level=trace  --verbose=debug --trace=.*:log.txt

$(WORK_DIR):
	mkdir -p $(WORK_DIR)

.PHONY: build
