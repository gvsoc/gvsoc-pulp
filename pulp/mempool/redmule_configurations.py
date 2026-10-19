import os
import gvsoc.systree as st

# Tensor engine model: 'redmule' (LightRedmule) or 'opope' (LightOpope, outer
# product engine with p x p MACs and a 2p x 16-bit port, X stored transposed).
# Read from the environment as the RedMulE shape, both by the standalone
# Tensorpool target and by the flex_cluster arch files.
_DEFAULT_ENGINE = os.environ.get('TENSOR_ENGINE', 'redmule')

# Standalone (single-cluster) Tensorpool RedMulE shape. TensorpoolSystem
# (pulp/pulp/mempool/mempool_system.py) instantiates RedmuleParam() with no
# arguments, so these env vars are the only way to resize RedMulE for that
# target without editing this file; the app must be rebuilt with matching
# redmule_height=/redmule_width=/redmule_regs= values.
_DEFAULT_REDMULE_HEIGHT = int(os.environ.get('REDMULE_HEIGHT', 4))
_DEFAULT_REDMULE_WIDTH = int(os.environ.get('REDMULE_WIDTH', 8))
_DEFAULT_REDMULE_REGS = int(os.environ.get('REDMULE_REGS', 3))

_DEFAULT_OPOPE_P = int(os.environ.get('OPOPE_P', 8))
_DEFAULT_OPOPE_MAC_LATENCY = int(os.environ.get('OPOPE_MAC_LATENCY', 0))

# Request queue entries of the tensor engine (both models): a request holds an
# entry until the array uses its data (or, for a store, until it is done).
_DEFAULT_QUEUE_DEPTH = int(os.environ.get('TE_QUEUE_DEPTH', 128))

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
        opope_mac_latency=_DEFAULT_OPOPE_MAC_LATENCY
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
