import gvsoc.systree as st

class RedmuleParam:
    def __init__(
        self,
        tcdm_bank_number=None,
        redmule_height=8,
        redmule_width=32,
        redmule_regs=3,
        queue_depth=128,
        stream_loads=True,
        row_refill_cyc=740
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