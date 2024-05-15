#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
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

class Sequencer(gvsoc.systree.Component):
    """Interconnect sequencer

    This models a sequence buffer for offloading instructions to the correponding subsystem.
    It has a single input port and a single output port.
    It routes incoming requests to the output port. There are three paths for different instructions.
    It can apply a latency to the output port. 
    (Normally, this model has no extra latency in bypass lane. 
    The sequenceable instruction has one cycle latency generating from read to read operation.)

    Attributes
    ----------
    parent: gvsoc.systree.Component
        The parent component where this one should be instantiated.
    name: str
        The name of the component within the parent space.
    latency: int
        Global latency applied to all incoming requests. This impacts the start time of the burst.
    """
    def __init__(self, parent: gvsoc.systree.Component, name: str, latency: int=0):
        super(Sequencer, self).__init__(parent, name)

        self.set_component('pulp.snitch.sequencer')

        self.add_property('latency', latency)

