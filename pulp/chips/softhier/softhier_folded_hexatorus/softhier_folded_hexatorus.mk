######################################################################
## 				Make Targets for SoftHier Simulator 				##
######################################################################

ACTUAL_TOPO ?= softhier_folded_hexatorus

third_party/toolchain:
	mkdir -p third_party/toolchain
	cd third_party/toolchain && \
	wget https://github.com/pulp-platform/pulp-riscv-gnu-toolchain/releases/download/v1.0.16/v1.0.16-pulp-riscv-gcc-centos-7.tar.bz2 &&\
	tar -xvjf v1.0.16-pulp-riscv-gcc-centos-7.tar.bz2 &&\
	wget https://github.com/husterZC/gun_toolchain/releases/download/v2.0.0/toolchain.tar.xz &&\
	tar -xvf toolchain.tar.xz

sh-config:
	python3 pulp/pulp/chips/softhier/common/utils/config.py folded_hexatorus \
		pulp/pulp/chips/softhier/common/sw/runtime/include $(if $(cfg),--arch-file $(cfg))

sh-hw:
	make sh-config
	make TARGETS=pulp.chips.softhier.topologies.$(ACTUAL_TOPO)_target all

######################################################################
## 				Make Targets for SoftHier Software	 				##
######################################################################

sw_cmake_arg ?= ""
ifdef app
	app_path = $(abspath $(app))
	sw_cmake_arg = "-DSRC_DIR=$(app_path)"
endif

arch_cmake_arg := "-DRISCV_ARCH=rv32imafdv_zfh"

sh-sw:
	make sh-config
	rm -rf sw_build && mkdir sw_build
	cd sw_build && $(CMAKE) $(sw_cmake_arg) $(arch_cmake_arg) ../pulp/pulp/chips/softhier/common/sw/ && make
	@! grep -q "ebreak" sw_build/softhier.dump || (echo "Error: 'ebreak' found in sw_build/softhier.dump" && exit 1)

sh-sw-clean:
	rm -rf sw_build


######################################################################
## 				Make Targets for Run Simulator		 				##
######################################################################

sh-run:
	./install/bin/gvsoc --target=pulp.chips.softhier.topologies.$(ACTUAL_TOPO)_target --binary sw_build/softhier.elf run