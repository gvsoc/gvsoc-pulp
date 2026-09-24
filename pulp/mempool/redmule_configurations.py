import os
import gvsoc.systree as st

# Standalone (single-cluster) Tensorpool RedMulE shape. TensorpoolSystem
# (pulp/pulp/mempool/mempool_system.py) instantiates RedmuleParam() with no
# arguments, so these env vars are the only way to resize RedMulE for that
# target without editing this file; the app must be rebuilt with matching
# redmule_height=/redmule_width=/redmule_regs= values.
_DEFAULT_REDMULE_HEIGHT = int(os.environ.get('REDMULE_HEIGHT', 4))
_DEFAULT_REDMULE_WIDTH = int(os.environ.get('REDMULE_WIDTH', 8))
_DEFAULT_REDMULE_REGS = int(os.environ.get('REDMULE_REGS', 3))

class RedmuleParam:
    def __init__(
        self,
        tcdm_bank_number=None,
        redmule_height=_DEFAULT_REDMULE_HEIGHT,
        redmule_width=_DEFAULT_REDMULE_WIDTH,
        redmule_regs=_DEFAULT_REDMULE_REGS,
        queue_depth=128,
        stream_loads=True,
        row_refill_cyc=30
    ):
        if tcdm_bank_number is not None:
            self.tcdm_bank_number = tcdm_bank_number
        else:
            self.tcdm_bank_number = (redmule_height * (redmule_regs + 1)) // 2

        self.redmule_height = redmule_height
        self.redmule_width = redmule_width
        self.redmule_regs = redmule_regs
        self.queue_depth = queue_depth
        self.stream_loads = stream_loads
        self.row_refill_cyc = row_refill_cyc