#!/usr/bin/env python3

import pexpect
import os
import sys


# The target can be given as first argument, so that the same Linux boot test can be
# run on the untimed rv64 board or on the timed cva6 one.
target = sys.argv[1] if len(sys.argv) > 1 else 'rv64_untimed'

# The binary path must be absolute since gvrun runs from its work directory
binary = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'spike_fw_payload.elf')

run = pexpect.spawn(f"gvrun --target {target} --param soc/binary={binary} run", encoding='utf-8', logfile=sys.stdout, env=os.environ)
match = run.expect(['NFS preparation skipped, OK'], timeout=None)
match = run.expect(['#'], timeout=None)
