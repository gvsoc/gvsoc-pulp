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

from config_tree import Config, cfg_field


class CollectiveTestbenchConfig(Config):
    """Configuration of the FlooNoC collective testbench (test.cpp)."""

    dim: int = cfg_field(default=0, desc="Dimension of the square mesh")
    node_size: int = cfg_field(default=0, fmt="hex", desc="Size of the memory of each node")
    node_base: int = cfg_field(default=0, fmt="hex", desc="Base address of the memory of node 0")
    scenario: str = cfg_field(default='', desc="Test scenario")
