# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""The RTL platform of pulp-open_v2, as the one of pulp-open: the RTL of PULP
(pulp-platform/pulp) with its testbench (rtl/tb/tb_pulp.sv), fetched and
compiled by the SDK's test.mk (test.checkout.pulp-rtl, then
test.build.pulp-rtl for gvsoc_svsim or test.build.pulp-rtl.questa for Questa)
under $PULP_RTL_HOME (tests/pulp-open of the SDK).

The testbench loads the binary into the L2 through JTAG from its stimuli file
(+stimuli=, one line per 64-bit word: address_data), starts the FC at
+ENTRY_POINT and polls the core status register of the APB SoC control until
the program writes its end-of-computation bit there, then stops with the
status in its exit_status variable. Its stdout window prints what the cores
write to it. PulpOS uses both on this platform (pulpos/io=stdout).

The simulator is gvrun's --rtl-simulator: gvsoc_svsim (default), which runs
the design as a GVSoC component through its GVSoC plugin, or questa, which
runs vsim on the library the Questa build compiled.
"""

import os
import shutil
import subprocess

import gvrun.runner
from gvrun.parameter import TargetParameter


# The parameters the RTL build gives the testbench (sim/Makefile of the RTL)
TB_PARAMS = {
    'USE_SDVT_SPI': '0', 'USE_SDVT_CPI': '0', 'BAUDRATE': '115200', 'ENABLE_DEV_DPI': '0',
    'LOAD_L2': 'JTAG', 'USE_SDVT_I2S': '0',
}

# The stimuli file, in the work directory
STIMULI = os.path.join('vectors', 'stim.txt')

SIMULATORS = ('gvsoc_svsim', 'questa')


def rtl_home():
    home = os.environ.get('PULP_RTL_HOME')
    if not home:
        raise RuntimeError('pulp-open_v2 (rtl): $PULP_RTL_HOME must be set (source the sourceme.sh of the SDK)')
    return home


def svsim_design():
    """The design compiled for gvsoc_svsim by test.build.pulp-rtl."""
    return os.path.join(rtl_home(), 'work', 'svsim', 'pulp.hsim')


def svsim_plugin():
    """The gvsoc_svsim GVSoC plugin: $GVSOC_SVSIM_PLUGIN, else next to the installed
    gvsoc_svsim of the SDK, in <install>/lib."""
    path = os.environ.get('GVSOC_SVSIM_PLUGIN')
    if path:
        return path
    exe = shutil.which('gvsoc_svsim')
    if exe:
        lib = os.path.join(os.path.dirname(os.path.realpath(exe)), '..', 'lib', 'libgvsoc_elab.so')
        if os.path.exists(lib):
            return os.path.normpath(lib)
    return None


def write_stimuli(elf_path, stim_path):
    """Write the loadable segments of the ELF as the testbench's stimuli: one
    `<address>_<64-bit data>` line per 8-byte word, at the load addresses."""
    from elftools.elf.elffile import ELFFile

    words = {}
    with open(elf_path, 'rb') as elf_file:
        for segment in ELFFile(elf_file).iter_segments():
            if segment['p_type'] != 'PT_LOAD' or segment['p_filesz'] == 0:
                continue
            addr = segment['p_paddr']
            for offset, byte in enumerate(segment.data()):
                byte_addr = addr + offset
                word = byte_addr & ~7
                words[word] = words.get(word, 0) | (byte << ((byte_addr & 7) * 8))
    os.makedirs(os.path.dirname(stim_path), exist_ok=True)
    with open(stim_path, 'w', encoding='utf-8') as file:
        for word in sorted(words):
            file.write('%08X_%016X\n' % (word, words[word]))


def prepare_run_dir(work_dir):
    """Give the run directory what the testbench finds in the sim directory of
    the RTL, where its Questa flow runs: the boot code of the ROM (boot), and the
    directories its file system and stdout handlers write to (fs, stdout)."""
    boot = os.path.join(work_dir, 'boot')
    if not os.path.lexists(boot):
        os.symlink(os.path.join(rtl_home(), 'pulp-rtl', 'sim', 'boot'), boot)
    for name in ('fs', 'stdout'):
        os.makedirs(os.path.join(work_dir, name), exist_ok=True)


def entry_point(elf_path):
    from elftools.elf.elffile import ELFFile
    with open(elf_path, 'rb') as elf_file:
        return ELFFile(elf_file)['e_entry']


def plusargs(elf_path, work_dir):
    # VSIM_PATH: the sim directory of the RTL, where its Questa flow runs, from
    # which the camera model finds its frames
    return [f'+stimuli={os.path.join(work_dir, STIMULI)}', '+ENTRY_POINT=%x' % entry_point(elf_path),
            f'+VSIM_PATH={os.path.join(rtl_home(), "pulp-rtl", "sim")}']


def make_board():
    """The board of the RTL platform: the testbench as a GVSoC component, run by
    the gvsoc_svsim plugin, or only holding the binary for the Questa runner."""
    from utils.verilator import VerilatorBoard

    class PulpOpenV2RtlBoard(VerilatorBoard):

        def __init__(self, parent, name=None, parser=None, options=None):
            super().__init__(parent, name, target_name='pulp-open')
            # The attributes of the chip, which the software reads (e.g. its
            # memory map), as the GVSoC board has them
            from pulp.chips.pulp_open_v2.attr import PulpOpenV2Attr
            from pulp.chips.pulp_open_v2.soc import PulpOpenV2SocConfig
            self.set_attributes(PulpOpenV2Attr(self, PulpOpenV2SocConfig()))
            TargetParameter(
                self, name='binary', value=None, cast=str,
                description='Binary to be loaded and started',
            )
            self._elf = None
            # Where the stimuli are written, set by the target
            self.work_dir = os.getcwd()

        def configure(self):
            # The testbench loads the binary from its stimuli file, not from the
            # firmwares of the RTL control
            binary = self.get_parameter('binary')
            self._elf = binary if binary is not None else next(iter(self._firmwares), None)
            self._firmwares = []
            if self.get_parameter('exit_signal') is None:
                self.set_parameter('exit_signal', 'tb_pulp.exit_status')
            super().configure()
            if self._elf is not None and os.path.exists(self._elf):
                self.verilator.add_property(
                    'plusargs', list(self.verilator.properties.get('plusargs', [])) +
                    plusargs(self._elf, self.work_dir))

        def elf(self):
            return self._elf

        def prepare_inputs(self):
            """Write the stimuli of the binary, after the build and before the run."""
            if self._elf is not None and os.path.exists(self._elf):
                write_stimuli(self._elf, os.path.join(self.work_dir, STIMULI))
            prepare_run_dir(self.work_dir)

    return PulpOpenV2RtlBoard


class QuestaRunner(gvrun.runner.Runner):
    """Runs the testbench on Questa (vsim), with the library compiled by
    test.build.pulp-rtl.questa in the sim directory of the RTL, as its
    Makefile's simc target does. The exit code is the testbench's
    exit_status."""

    def __init__(self, target):
        self.target = target

    def run(self, args):
        elf = self.target.model.elf()
        if elf is None:
            raise RuntimeError('pulp-open_v2 (rtl, questa): no binary to run')
        vsim = shutil.which('vsim')
        if vsim is None:
            raise RuntimeError('pulp-open_v2 (rtl, questa): vsim not found, set up Questa first')
        sim_dir = os.path.join(rtl_home(), 'pulp-rtl', 'sim')
        if not os.path.isdir(os.path.join(sim_dir, 'work')):
            raise RuntimeError(f'pulp-open_v2 (rtl, questa): no compiled library in {sim_dir}; build it with: '
                               'make test.build.pulp-rtl.questa')
        work_dir = self.target.model.work_dir
        write_stimuli(elf, os.path.join(work_dir, STIMULI))
        prepare_run_dir(work_dir)
        command = [vsim, '-64', '-c', 'vopt_tb', '-modelsimini', os.path.join(sim_dir, 'modelsim.ini'),
                   '-L', 'models_lib', '-L', 'vip_lib', '-suppress', 'vsim-3009', '-suppress', 'vsim-8683',
                   '+UVM_NO_RELNOTES', '-t', 'ps',
                   '-sv_lib', os.path.join(sim_dir, '..', 'rtl', 'tb', 'remote_bitbang', 'librbs')]
        command += [f'-g{name}={value}' for name, value in TB_PARAMS.items()]
        command += plusargs(elf, work_dir)
        command += ['-do', 'run -all', '-do', 'quit -code [examine -radix decimal sim:/tb_pulp/exit_status]']
        return subprocess.run(command, check=False, cwd=work_dir).returncode
