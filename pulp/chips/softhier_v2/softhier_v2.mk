# Select the topology depending on the TOPOLOGY variable.
ifeq ($(TOPOLOGY),)
    ACTUAL_TOPO = softhier_2d_mesh
else ifeq ($(findstring softhier,$(TOPOLOGY)),)
    ACTUAL_TOPO = softhier_$(TOPOLOGY)
else
    ACTUAL_TOPO = $(TOPOLOGY)
endif

# The name config.py expects: ACTUAL_TOPO minus its "softhier_" prefix
# (softhier_arch_base.TOPOLOGIES key).
TOPO_NAME = $(patsubst softhier_%,%,$(ACTUAL_TOPO))

empty :=
space := $(empty) $(empty)
comma := ,
# PARAMS=key=value,... overrides individual arch attributes. They reach the
# software headers through config.py, and the platform as target parameters
# (<target>:system/key=value,...), both at build and at run time.
PARAM_FLAGS = $(foreach p,$(subst $(comma),$(space),$(PARAMS)),--param $(p))
TARGET_PARAMS = $(if $(PARAMS),:$(subst $(space),$(comma),$(foreach p,$(subst $(comma),$(space),$(PARAMS)),system/$(p))))
SH2_TARGET = pulp.chips.softhier_v2.topologies.$(ACTUAL_TOPO)_target$(TARGET_PARAMS)

######################################################################
## 				Make Targets for SoftHier Simulator 				##
######################################################################

third_party/toolchain:
	mkdir -p third_party/toolchain
	cd third_party/toolchain && \
	wget https://github.com/pulp-platform/pulp-riscv-gnu-toolchain/releases/download/v1.0.16/v1.0.16-pulp-riscv-gcc-centos-7.tar.bz2 &&\
	tar -xvjf v1.0.16-pulp-riscv-gcc-centos-7.tar.bz2 &&\
	wget https://github.com/husterZC/gun_toolchain/releases/download/v2.0.0/toolchain.tar.xz &&\
	tar -xvf toolchain.tar.xz

# The NoC topology is generated from the arch when the platform is built,
# only the software headers need a configuration step.
sh2-config:
	python3 pulp/pulp/chips/softhier_v2/common/utils/config.py $(TOPO_NAME) \
		pulp/pulp/chips/softhier_v2/common/sw/runtime/include $(if $(cfg),--arch-file $(cfg)) $(PARAM_FLAGS)

sh2-hw:
	make TARGETS=$(SH2_TARGET) all

######################################################################
## 				Make Targets for SoftHier Software	 				##
######################################################################

sw_cmake_arg ?= ""
ifdef app
	app_path = $(abspath $(app))
	sw_cmake_arg = "-DSRC_DIR=$(app_path)"
endif

arch_cmake_arg := "-DRISCV_ARCH=rv32imafdv_zfh"

sh2-sw:
	make sh2-config
	rm -rf sw_build && mkdir sw_build
	cd sw_build && $(CMAKE) $(sw_cmake_arg) $(arch_cmake_arg) ../pulp/pulp/chips/softhier_v2/common/sw/ && make
	@! grep -q "ebreak" sw_build/softhier.dump || (echo "Error: 'ebreak' found in sw_build/softhier.dump" && exit 1)

sh2-sw-clean:
	rm -rf sw_build

######################################################################
## 				Make Targets for Run Simulator		 				##
######################################################################

sh2-run:
	./install/bin/gvrun --target $(SH2_TARGET) --work-dir sw_build --parameter system/binary=sw_build/softhier.elf run $(RUN_ARGS)

# Forward DEBUG to the top-level build, not just to make itself.
build: export DEBUG := $(DEBUG)
