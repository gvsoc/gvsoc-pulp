#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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

import gvsoc.systree
import regmap.regmap
import regmap.regmap_hjson
import regmap.regmap_c_header


class QuadCfg(gvsoc.systree.Component):

    def __init__(self, parent: gvsoc.systree.Component, name: str):

        super().__init__(parent, name)

        self.add_sources(['pulp/chips/occamy/quad_cfg.cpp'])

        self.add_properties({
        })

    def gen(self, builddir, installdir):
        comp_path = 'pulp/chips/occamy'
        regmap_path = f'{comp_path}/occamy_quadrant_s1_reg.hjson'
        regmap_instance = regmap.regmap.Regmap('quad_cfg')
        regmap.regmap_hjson.import_hjson(regmap_instance, self.get_file_path(regmap_path))
        regmap.regmap_c_header.dump_to_header(regmap=regmap_instance, name='quad_cfg',
            header_path=f'{builddir}/{comp_path}/quad_cfg', headers=['regfields', 'gvsoc'])


    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    def o_QUADRANT_RESET(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('quadrant_reset', itf, signature='wire<bool>')
