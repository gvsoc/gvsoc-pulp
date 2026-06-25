#
# Copyright (C) 2022-2026 ETH Zurich and University of Bologna
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

set(HWPE_SRCS
"src/input_load.cpp"
"src/input_layout.cpp"
"src/weight_load.cpp"
"src/weight_layout.cpp"
"src/hwpe_weightoffset.cpp"
"src/hwpe_compute.cpp"
"src/output_store.cpp"
"src/hwpe_fsm.cpp"
"src/hwpe.cpp"
    )
vp_model(NAME pulp.simple_hwpe.hwpe
    SOURCES ${HWPE_SRCS}
    )
set(HWPE_INCS
"inc/binconv"
"inc/buffer"
"inc"
)

vp_model_include_directories(
    NAME pulp.simple_hwpe.hwpe
    DIRECTORY ${HWPE_INCS}
    )

vp_model_compile_options(NAME pulp.simple_hwpe.hwpe OPTIONS)