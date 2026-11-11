import os
import gvsoc.systree as st

# Tensor engine model: 'redmule' or 'opope'.
_DEFAULT_ENGINE = os.environ.get('TENSOR_ENGINE', 'redmule')

# Standalone (single-cluster) Tensorpool RedMulE shape.
_DEFAULT_REDMULE_HEIGHT = int(os.environ.get('REDMULE_HEIGHT', 8))
_DEFAULT_REDMULE_WIDTH = int(os.environ.get('REDMULE_WIDTH', 32))
_DEFAULT_REDMULE_REGS = int(os.environ.get('REDMULE_REGS', 3))

# Standalone (single-cluster) Tensorpool O-POPE shape.
_DEFAULT_OPOPE_P = int(os.environ.get('OPOPE_P', 8))
_DEFAULT_OPOPE_MAC_LATENCY = int(os.environ.get('OPOPE_MAC_LATENCY', 0))

# A request holds an entry until the array uses its data (or, for a store, until it is done).
_DEFAULT_QUEUE_DEPTH = int(os.environ.get('TE_QUEUE_DEPTH', 256))
# Cycles an L1 bank is held per tensor-engine access (see l1_subsystem.py)
_DEFAULT_BANK_HOLD = int(os.environ.get('L1_BANK_HOLD', 9))
# Cycles before the first job of each TE run (paid once per run)
_DEFAULT_FIRST_JOB_LATENCY = int(os.environ.get('TE_FIRST_JOB_LATENCY', 0))

class RedmuleParam:
    def __init__(
        self,
        tcdm_bank_number=None,
        redmule_height=_DEFAULT_REDMULE_HEIGHT,
        redmule_width=_DEFAULT_REDMULE_WIDTH,
        redmule_regs=_DEFAULT_REDMULE_REGS,
        queue_depth=_DEFAULT_QUEUE_DEPTH,
        stream_loads=True,
        engine=_DEFAULT_ENGINE,
        opope_p=_DEFAULT_OPOPE_P,
        opope_mac_latency=_DEFAULT_OPOPE_MAC_LATENCY,
        first_job_latency=_DEFAULT_FIRST_JOB_LATENCY
    ):
        assert engine in ('redmule', 'opope'), f"unknown tensor engine '{engine}'"
        if tcdm_bank_number is not None:
            self.tcdm_bank_number = tcdm_bank_number
        elif engine == 'opope':
            # 2p x 16-bit port on 32-bit banks
            self.tcdm_bank_number = opope_p
        else:
            self.tcdm_bank_number = (redmule_height * (redmule_regs + 1)) // 2

        self.redmule_height = redmule_height
        self.redmule_width = redmule_width
        self.redmule_regs = redmule_regs
        self.queue_depth = queue_depth
        self.stream_loads = stream_loads
        self.engine = engine
        self.opope_p = opope_p
        self.opope_mac_latency = opope_mac_latency
        self.first_job_latency = first_job_latency
