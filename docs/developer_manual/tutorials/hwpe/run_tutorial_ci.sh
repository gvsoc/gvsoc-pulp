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

echo "verifying HWPE tutorial"

# An array of tasks with key-value pair
declare -A tasks=(
    ["task1"]="model_hwpe_sol1"
    ["task2"]="model_hwpe_sol2"
    ["task3"]="model_hwpe_sol3"
    ["task4"]="model_hwpe_sol4"
    ["task5"]="model_hwpe_sol5"
    ["task6"]="model_hwpe_sol6"
    ["task7"]="model_hwpe_sol7"
    ["task8"]="model_hwpe_sol8"
)

# Define the order of tasks - tutorial is designed to be dependent on the previous tasks
ordered_tasks=("task1" "task2" "task3" "task4" "task5" "task6" "task7" "task8")

# Base directory for traces
output_trace_base="traces"

# Run initial steps to make the source files correct
mkdir -p traces
echo "Running initial setup commands..."
make -C pulp/docs/developer_manual/tutorials/hwpe create_target_sol_task4
make -C pulp/docs/developer_manual/tutorials/hwpe integrate_hwpe_sol10
echo "Initial setup completed."

# Loop through each task in the specified order and perform the operations
for task in "${ordered_tasks[@]}"; do
    model_step=${tasks[$task]}
    output_trace="$output_trace_base/model_hwpe_${task}.txt"

    echo "--------------------------------------------"
    echo "Processing $model_step ($task)"
    
    # Execute the commands to source relevant files and build gvsoc
    echo "Building $model_step"
    make -C pulp/docs/developer_manual/tutorials/hwpe $model_step
    echo "Building with make build TARGETS=pulp-open-hwpe"
    make -C pulp/docs/developer_manual/tutorials/hwpe build TARGETS=pulp-open-hwpe

    # Extract the task number from the task name (e.g., "task2" -> 2)
    task_number=${task:4}
    
    # Set binary path based on the task number
    case $task_number in
        1) binary_path="./pulp/docs/developer_manual/tutorials/hwpe/model_hwpe/application/task1/test" ;;
        2) binary_path="./pulp/docs/developer_manual/tutorials/hwpe/model_hwpe/application/task2/test" ;;
        3) binary_path="./pulp/docs/developer_manual/tutorials/hwpe/model_hwpe/application/task3/test" ;;
        4|5|6|7) binary_path="./pulp/docs/developer_manual/tutorials/hwpe/model_hwpe/application/task4/test" ;;
        8) binary_path="./pulp/docs/developer_manual/tutorials/hwpe/model_hwpe/application/task8/test" ;;
        *) echo "Error: Invalid task number"; exit 1 ;;
    esac

    echo "Running gvsoc for $model_step ($task)"
    ./install/bin/gvsoc --target=pulp-open-hwpe --binary $binary_path run --trace=hwpe > $output_trace

    # Compare the output trace with the expected trace using Python script
    echo "Comparing output trace for $task using Python script"
    python3 ./pulp/docs/developer_manual/tutorials/hwpe/test_traces.py $task_number
    
    if [ $? -ne 0 ]; then
        echo "Error: Trace comparison failed for $task."
        exit 1
    else
        echo "Success: Trace comparison passed for $task."
    fi
done
