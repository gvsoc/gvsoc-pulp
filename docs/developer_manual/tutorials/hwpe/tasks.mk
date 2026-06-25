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

.PHONY: create_target_task integrate_hwpe_task

# this target sets everything up in the gvsoc-pulp folder to run the tutorial T1, and creates solutions in create_target/solutions
create_target_task:
	cp $(SUBTASKS_MK_DIR)../../../../../pulp/pulp-open.py $(SUBTASKS_MK_DIR)../../../../../pulp/pulp-open-hwpe.py && \
	patch $(SUBTASKS_MK_DIR)../../../../../pulp/pulp-open-hwpe.py < $(SUBTASKS_MK_DIR)create_target/task_setup_diffs/pulp-open.py.patch && \
	rm -rf $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe && \
	cp -r $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe && \
	patch $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe/pulp_open.py < $(SUBTASKS_MK_DIR)create_target/task_setup_diffs/pulp_open/pulp_open.py.patch && \
	patch $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe/pulp_open_board.py < $(SUBTASKS_MK_DIR)create_target/task_setup_diffs/pulp_open/pulp_open_board.py.patch && \
	patch $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe/cluster.py < $(SUBTASKS_MK_DIR)create_target/task_setup_diffs/pulp_open/cluster.py.patch && \
	cp $(SUBTASKS_MK_DIR)../../../../../pulp/pulp-open-hwpe.py $(SUBTASKS_MK_DIR)create_target/solutions/task3/pulp-open-hwpe.py && \
	patch $(SUBTASKS_MK_DIR)create_target/solutions/task3/pulp-open-hwpe.py < $(SUBTASKS_MK_DIR)create_target/solutions_diff/pulp-open-hwpe.py.patch && \
	cp $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe/pulp_open.py $(SUBTASKS_MK_DIR)create_target/solutions/task4/pulp_open.py && \
	patch $(SUBTASKS_MK_DIR)create_target/solutions/task4/pulp_open.py < $(SUBTASKS_MK_DIR)create_target/solutions_diff/pulp_open_hwpe/pulp_open.py.patch && \
	cp $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe/pulp_open_board.py $(SUBTASKS_MK_DIR)create_target/solutions/task4/pulp_open_board.py && \
	patch $(SUBTASKS_MK_DIR)create_target/solutions/task4/pulp_open_board.py < $(SUBTASKS_MK_DIR)create_target/solutions_diff/pulp_open_hwpe/pulp_open_board.py.patch && \
	cp $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe/cluster.py $(SUBTASKS_MK_DIR)create_target/solutions/task4/cluster.py && \
	patch $(SUBTASKS_MK_DIR)create_target/solutions/task4/cluster.py < $(SUBTASKS_MK_DIR)create_target/solutions_diff/pulp_open_hwpe/cluster.py.patch \
	|| { echo "ERROR: Failed to apply patches for hwpe tutorial!"; exit 1; }

# this target sets everything up in the gvsoc-pulp folder to run the tutorial T2, and creates solutions in create_target/solutions
integrate_hwpe_task:
	patch $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe/cluster.py < $(SUBTASKS_MK_DIR)integrate_hwpe/task_files_diff/cluster.py.patch && \
	patch $(SUBTASKS_MK_DIR)../../../../../pulp/pulp/chips/pulp_open_hwpe/l1_subsystem.py < $(SUBTASKS_MK_DIR)integrate_hwpe/task_files_diff/l1_subsystem.py.patch \
	|| { echo "ERROR: Failed to apply patches for hwpe tutorial!"; exit 1; }

model_hwpe_task1:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/task_files/task1/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_task2:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/task_files/task2/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_task3:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/task_files/task3/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_task4:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/task_files/task4/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_task5:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/task_files/task5/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_task6:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/task_files/task6/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_task7:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/task_files/task7/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
model_hwpe_task8:
	$(MAKE) SRC_DIR=$(SUBTASKS_MK_DIR)model_hwpe/task_files/task8/simple_hwpe DEST_DIR=$(SUBTASKS_MK_DIR)../../../../../pulp/pulp/simple_hwpe replace_folder
