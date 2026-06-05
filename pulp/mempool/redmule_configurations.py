import gvsoc.systree as st

class RedmuleParam:
    def __init__(self, redmule_height, redmule_width, redmule_regs):
        #if redmule enabled (nb_redmule_tiles>0), these first 3 need to configured, otherwise NULL objects will be derefenced
        self.redmule_height = redmule_height
        self.redmule_width = redmule_width
        self.redmule_regs = redmule_regs
        #self.rob_depth = rob_depth #not used currently