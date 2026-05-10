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

config_file_hexamesh ?= "pulp/pulp/chips/softhier_hexamesh/softhier_arch.py"
ifdef cfg
	config_file_hexamesh = "$(cfg)"
endif

sh-hexamesh-config:
	@if [ "$(config_file_hexamesh)" != "pulp/pulp/chips/softhier_hexamesh/softhier_arch.py" ]; then \
		cp -f $(config_file_hexamesh) pulp/pulp/chips/softhier_hexamesh/softhier_arch.py; \
	fi
	python3 pulp/pulp/chips/softhier_hexamesh/utils/config.py $(config_file_hexamesh)

sh-hexamesh-hw:
	make sh-hexamesh-config
	make TARGETS=pulp.chips.softhier_hexamesh.softhier_target all

######################################################################
## 				Make Targets for SoftHier Software	 				##
######################################################################

sw_cmake_arg ?= ""
ifdef app
	app_path = $(abspath $(app))
	sw_cmake_arg = "-DSRC_DIR=$(app_path)"
endif

arch_cmake_arg := "-DRISCV_ARCH=rv32imafdv_zfh"

sh-hexamesh-sw:
	rm -rf sw_build && mkdir sw_build
	cd sw_build && $(CMAKE) $(sw_cmake_arg) $(arch_cmake_arg) ../pulp/pulp/chips/softhier_hexamesh/sw/ && make
	@! grep -q "ebreak" sw_build/softhier.dump || (echo "Error: 'ebreak' found in sw_build/softhier.dump" && exit 1)

sh-hexamesh-sw-clean:
	rm -rf sw_build


######################################################################
## 				Make Targets for Run Simulator		 				##
######################################################################

sh-hexamesh-run:
	./install/bin/gvsoc --target=pulp.chips.softhier_hexamesh.softhier_target --binary sw_build/softhier.elf run

######################################################################
##              Make Targets for Automated Test Suite               ##
######################################################################

# Define the base software directory (where CMakeLists.txt lives)
SW_BASE_DIR = pulp/pulp/chips/softhier_hexamesh/sw

# Default to the test name if no TEST variable is provided
TEST ?= 00_init

# Automatically insert the /tests/ folder into the absolute path
TEST_DIR = $(abspath $(SW_BASE_DIR)/tests/$(TEST))

# Build target: Compiles whatever is inside $(TEST_DIR)
sh-hexamesh-test-sw:
	rm -rf sw_build && mkdir sw_build
	cd sw_build && $(CMAKE) "-DSRC_DIR=$(TEST_DIR)" $(arch_cmake_arg) ../$(SW_BASE_DIR) && make
	@! grep -q "ebreak" sw_build/softhier.dump || (echo "Error: 'ebreak' found in sw_build/softhier.dump" && exit 1)

# Run target: Executes the freshly built binary
sh-hexamesh-test-run: sh-hexamesh-test-sw
	./install/bin/gvsoc --target=pulp.chips.softhier_hexamesh.softhier_target --binary sw_build/softhier.elf run