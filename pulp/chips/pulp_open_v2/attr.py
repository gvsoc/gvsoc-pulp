# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""Attributes of pulp-open_v2, which the software reads (the PulpOS pulp-open
port links in soc/l2/range). Same tree as the pulp-open one, with the L2 of
this chip."""

from gvrun.attribute import Tree, Area, Value


class L2SharedAttr(Tree):
    def __init__(self, parent, name, base: int, size: int, nb_banks: int):
        super().__init__(parent, name)
        self.nb_banks = Value(self, 'nb_banks', nb_banks)
        self.nb_regions = Value(self, 'nb_regions', 12)
        self.interleaving_bits = Value(self, 'interleaving_bits', 2)
        self.range = Area(self, 'range', base, size, description='L2 shared banks')


class L2Attr(Tree):
    def __init__(self, parent, name, base: int, priv_size: int, shared_base: int,
                 shared_size: int, shared_banks: int):
        super().__init__(parent, name)
        self.range = Area(self, 'range', base, shared_base + shared_size - base,
            description='L2 whole address range')
        self.priv0 = Area(self, 'priv0', base, priv_size, description='L2 private bank 0')
        self.priv1 = Area(self, 'priv1', base + priv_size, priv_size,
            description='L2 private bank 1')
        self.shared = L2SharedAttr(self, 'shared', shared_base, shared_size, shared_banks)


class SocAttr(Tree):
    def __init__(self, parent, name, soc_config):
        super().__init__(parent, name)
        self.l2 = L2Attr(self, 'l2', soc_config.l2_base, soc_config.l2_priv_size,
            soc_config.l2_shared_base, soc_config.l2_shared_size, soc_config.l2_shared_banks)


class PulpOpenV2Attr(Tree):
    def __init__(self, parent, soc_config):
        super().__init__(parent)
        self.soc = SocAttr(self, 'soc', soc_config)
