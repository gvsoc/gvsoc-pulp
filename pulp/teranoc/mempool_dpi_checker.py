#
# Copyright (C) 2026 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Description: GVSoC support for MemPool/TeraNoC DPI-style result checks.
# Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)
#

import gvsoc.systree


class MempoolDpiChecker(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str, *,
                 nb_banks: int, bank_width: int, interleave: int,
                 l2_base: int, l2_size: int,
                 check_count_addr: int=0, check_table_addr: int=0):

        super().__init__(parent, name)

        self.add_sources(['pulp/teranoc/mempool_dpi_checker.cpp'])

        self.add_properties({
            'nb_banks': nb_banks,
            'bank_width': bank_width,
            'interleave': interleave,
            'l2_base': l2_base,
            'l2_size': l2_size,
            'check_count_addr': check_count_addr,
            'check_table_addr': check_table_addr,
        })

    def set_symbols(self, check_count_addr: int, check_table_addr: int):
        self.add_properties({
            'check_count_addr': check_count_addr,
            'check_table_addr': check_table_addr,
        })

