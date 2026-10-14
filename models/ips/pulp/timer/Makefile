# Regenerate the timer register-map headers under archi/ from the reference.md
# source. Re-run `make` from this directory whenever you edit the .md file.
#
# Resolves regmap-gen from PATH first, else falls back to $SDK_HOME/install/bin.

REGMAP_GEN ?= $(shell command -v regmap-gen 2>/dev/null || echo $(SDK_HOME)/install/bin/regmap-gen)

ARCHI := archi

HEADERS := \
    $(ARCHI)/timer_unit_gvsoc.h

.PHONY: all clean
all: $(HEADERS)

$(ARCHI)/timer_unit_gvsoc.h: TIMER_UNIT_reference.md | $(ARCHI)
	$(REGMAP_GEN) --input-md $< --name timer_unit --header $(ARCHI)/timer_unit

$(ARCHI):
	mkdir -p $@

clean:
	rm -rf $(ARCHI)
