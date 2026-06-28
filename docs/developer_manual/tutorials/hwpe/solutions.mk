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


# include /helpers.mk

.PHONY: create_target_sol integrate_hwpe_sol

create_target_sol:
	$(MAKE) SRC_FPATH=$(SUBTASKS_MK_DIR)create_target/solutions/task3/pulp-open-hwpe.py DEST_FPATH=$(SUBTASKS_MK_DIR)../../../../../pulp/ copy_file
	$(call check_and_create_dir,$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe)
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)create_target/solutions/task4 DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe copy_folder

integrate_hwpe_sol:
	$(MAKE) SRC_FPATH=$(SUBTASKS_MK_DIR)integrate_hwpe/solutions/pulp_open_hwpe/cluster.json DEST_FPATH=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe copy_file
	patch $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe/cluster.py < $(SUBTASKS_MK_DIR)integrate_hwpe/solutions_diff/pulp_open_hwpe/cluster.py.patch && \
	patch $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe/l1_subsystem.py < $(SUBTASKS_MK_DIR)integrate_hwpe/solutions_diff/pulp_open_hwpe/l1_subsystem.py.patch \
	|| { echo "ERROR: Failed to apply solution patches for hwpe tutorial!"; exit 1; }
	patch $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/CMakeLists.txt < $(SUBTASKS_MK_DIR)integrate_hwpe/solutions_diff/CMakeLists.txt.patch \
	|| { echo "WARNING: Failed to apply CMakeLists patch for hwpe tutorial, likely this is ok..."; }

model_hwpe_sol1:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/solutions/task1/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_sol2:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/solutions/task2/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_sol3:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/solutions/task3/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_sol4:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/solutions/task4/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_sol5:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/solutions/task5/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_sol6:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/solutions/task6/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_sol7:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/solutions/task7/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_sol8:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/solutions/task8/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder