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

config_file ?= "pulp/pulp/chips/softhier/softhier_arch.py"
ifdef cfg
	config_file = "$(cfg)"
endif

sh-config:
	@if [ "$(config_file)" != "pulp/pulp/chips/softhier/softhier_arch.py" ]; then \
		cp -f $(config_file) pulp/pulp/chips/softhier/softhier_arch.py; \
	fi
	python3 pulp/pulp/chips/softhier/utils/config.py $(config_file)

sh-hw:
	make sh-config
	make TARGETS=pulp.chips.softhier.softhier_target all

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
	rm -rf sw_build && mkdir sw_build
	cd sw_build && $(CMAKE) $(sw_cmake_arg) $(arch_cmake_arg) ../pulp/pulp/chips/softhier/sw/ && make
	@! grep -q "ebreak" sw_build/softhier.dump || (echo "Error: 'ebreak' found in sw_build/softhier.dump" && exit 1)

sh-sw-clean:
	rm -rf sw_build


######################################################################
## 				Make Targets for Run Simulator		 				##
######################################################################

sh-run:
	./install/bin/gvsoc --target=pulp.chips.softhier.softhier_target --binary sw_build/softhier.elf run
