/*
    Copyright (c) 2025 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <iostream>
#include <vector>
#include <algorithm>
#include <atomic>
#include <random>
#include <chrono>
#include <memory>
#include <queue>
#include <unordered_set>

#include "oneapi/tbb.h"

const int initial_depth_threshold = 10;

/*begin_search_serial*/
// Binary tree node structure
struct TreeNode {
    int value;
    TreeNode* left = nullptr;
    TreeNode* right = nullptr;
    
    // Destructor to clean up children
    ~TreeNode() {
        delete left;
        delete right;
    }
};

void serial_tree_search(TreeNode* node, int target, TreeNode*& result) {
    if (node && !result) {
        if (node->value == target) {
            result = node;
        } else {
            serial_tree_search(node->left, target, result);
            serial_tree_search(node->right, target, result);
        }
    }
}
/*end_search_serial*/

/*begin_parallel_invoke_search*/
void sequential_tree_search(TreeNode* node, int target, std::atomic<TreeNode*>& result) {
    if (node && !result.load()) { 
        if (node->value == target) {
            result.store(node);
        } else {
            sequential_tree_search(node->left, target, result);
            sequential_tree_search(node->right, target, result);
        }
    }
}

void parallel_invoke_search(TreeNode* node, int target, std::atomic<TreeNode*>& result,
                            size_t depth_threshold = initial_depth_threshold) {
    if (node && !result.load()) { 
        if (node->value == target) {
            result.store(node);
        } else if (depth_threshold == 0) {
            sequential_tree_search(node, target, result);
        } else {
            tbb::parallel_invoke(
                [&] { parallel_invoke_search(node->left, target, result, depth_threshold - 1);  },
                [&] { parallel_invoke_search(node->right, target, result, depth_threshold - 1); }
            );
        }
    }
}
/*end_parallel_invoke_search*/

/*begin_parallel_search*/
void parallel_tree_search_impl(tbb::task_group& tg, TreeNode* node, int target, 
                               std::atomic<TreeNode*>& result,
                               size_t depth_threshold = initial_depth_threshold) {
    if (node && !result.load()) { 
        if (node->value == target) {
            result.store(node);
        } else if (depth_threshold == 0) {
            // at parallel cutoff
            sequential_tree_search(node, target, result);
        } else if (node->left) {
            if (node->right) {
                // have two children, run the right as independent task
                tg.run([node, target, &result, &tg, depth_threshold] {
                    parallel_tree_search_impl(tg, node->right, target, result, depth_threshold - 1);
                });
            }
            parallel_tree_search_impl(tg, node->left, target, result, depth_threshold - 1);
        } else if (node->right) {
            parallel_tree_search_impl(tg, node->right, target, result, depth_threshold - 1);
        }
    }
}

TreeNode* parallel_tree_search(TreeNode* root, int target) {
    if (!root) return nullptr;
    
    std::atomic<TreeNode*> result{nullptr};
    tbb::task_group tg;
    
    // Start the divide and conquer search with a single task group
    parallel_tree_search_impl(tg, root, target, result);
    
    // Wait for all tasks to complete at the outermost level
    tg.wait();
    
    return result.load();
}
/*end_parallel_search*/

/*begin_parallel_search_cancellation*/
void parallel_tree_search_cancellable_impl(tbb::task_group& tg, TreeNode* node, int target, 
                                           std::atomic<TreeNode*>& result, 
                                           tbb::task_group_context& ctx,
                                           size_t depth_threshold = initial_depth_threshold) {
    if (node && !ctx.is_group_execution_cancelled()) {
       if (node->value == target) {
            result.store(node);
            ctx.cancel_group_execution(); // Cancel remaining tasks
       } else if (depth_threshold == 0) {
            sequential_tree_search(node, target, result);
            if (result.load() != nullptr) {
                ctx.cancel_group_execution();
            }
        } else if (node->left) {
            if (node->right) {
                // have two children, run the right one in the task_group
                tg.run([node, target, &result, &tg, &ctx, depth_threshold] {
                    parallel_tree_search_cancellable_impl(tg, node->right, target, result, ctx, depth_threshold - 1);
                });
            }
            parallel_tree_search_cancellable_impl(tg, node->left, target, result, ctx, depth_threshold - 1);
        } else if (node->right) {
            parallel_tree_search_cancellable_impl(tg, node->right, target, result, ctx, depth_threshold - 1);
        }
    }
}

TreeNode* parallel_tree_search_cancellable(TreeNode* root, int target) {
    if (!root) return nullptr;
    
    std::atomic<TreeNode*> result{nullptr};
    tbb::task_group_context ctx;
    tbb::task_group tg(ctx);
    
    // Start the divide and conquer search with cancellation support
    parallel_tree_search_cancellable_impl(tg, root, target, result, ctx);
    
    // Wait for all tasks to complete or be cancelled
    tg.wait();

    return result.load();
}
/*end_parallel_search_cancellation*/

// Helper function to generate a random binary tree
TreeNode* generate_random_tree(size_t num_nodes, std::mt19937& gen, 
                               std::uniform_int_distribution<int>& dist, int& target) {
    if (num_nodes == 0) return nullptr;
    
    // Generate unique values for all nodes using unordered_set for fast lookup
    std::vector<int> unique_values;
    std::unordered_set<int> used_values;
    unique_values.reserve(num_nodes);
    
    while (unique_values.size() < num_nodes) {
        int value = dist(gen);
        // Check if value is already used (O(1) average time complexity)
        if (used_values.find(value) == used_values.end()) {
            unique_values.push_back(value);
            used_values.insert(value);
        }
    }
    
    // Build tree using unique values
    auto root = new TreeNode{unique_values[0]};
    std::queue<TreeNode*> queue;
    queue.push(root);
    
    size_t value_index = 1;
    
    while (!queue.empty() && value_index < num_nodes) {
        TreeNode* current = queue.front();
        queue.pop();
        
        // Add left child
        if (value_index < num_nodes) {
            current->left = new TreeNode{unique_values[value_index]};
            queue.push(current->left);
            value_index++;
            if (value_index == num_nodes)
                target = current->left->value;
        }
        
        // Add right child
        if (value_index < num_nodes) {
            current->right = new TreeNode{unique_values[value_index]};
            queue.push(current->right);
            value_index++;
            if (value_index == num_nodes)
                target = current->right->value;
        }
    }
    
    return root;
}

// Example usage and test function
int main() {
    // Generate binary tree with 1 million nodes
    const size_t num_nodes = 10000000;
    
    // Random number generation setup
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(1, 100000000);
    
    std::cout << "Generating binary tree with " << num_nodes << " nodes...\n";
    int target = -1;
    TreeNode* root = generate_random_tree(num_nodes, gen, dist, target);
    
    // Find a value that exists in the tree (use the root value as target)

    std::cout << "Target value: " << target << " (guaranteed to exist)\n";
    std::cout << "Testing tree search algorithms on " << num_nodes << " nodes:\n";
    
    // Serial version with timing
    std::cout << "\nSerial tree search:\n";
    auto start = std::chrono::high_resolution_clock::now();
    TreeNode* serial_result = nullptr;
    serial_tree_search(root, target, serial_result);
    auto end = std::chrono::high_resolution_clock::now();
    auto serial_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    if (serial_result) {
        std::cout << "Found " << target << " (node address: " << serial_result << ")\n";
    } else {
        std::cout << "Target " << target << " not found" << std::endl;
    }
    std::cout << "Serial search time: " << serial_duration.count() << " us\n";
        
    // parallel_invoke version with timing
    std::cout << "\nparallel_invoke search:\n";
    start = std::chrono::high_resolution_clock::now();
    TreeNode* parallel_invoke_result = parallel_tree_search(root, target);
    end = std::chrono::high_resolution_clock::now();
    auto parallel_invoke_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    if (parallel_invoke_result) {
        std::cout << "Found " << target << " (node address: " << parallel_invoke_result << ")\n";
    } else {
        std::cout << "Target " << target << " not found" << std::endl;
    }
    std::cout << "Parallel search time: " << parallel_invoke_duration.count() << " us\n";

    // Parallel version (basic) with timing
    std::cout << "\nParallel tree search (basic):\n";
    start = std::chrono::high_resolution_clock::now();
    TreeNode* parallel_result = parallel_tree_search(root, target);
    end = std::chrono::high_resolution_clock::now();
    auto parallel_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    if (parallel_result) {
        std::cout << "Found " << target << " (node address: " << parallel_result << ")\n";
    } else {
        std::cout << "Target " << target << " not found" << std::endl;
    }
    std::cout << "Parallel search time: " << parallel_duration.count() << " us\n";
    
    // Parallel version with cancellation and timing
    std::cout << "\nParallel tree search (with cancellation):\n";
    start = std::chrono::high_resolution_clock::now();
    TreeNode* cancellation_result = parallel_tree_search_cancellable(root, target);
    end = std::chrono::high_resolution_clock::now();
    auto cancellation_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    if (cancellation_result) {
        std::cout << "Found " << target << " (node address: " << cancellation_result << ")\n";
    } else {
        std::cout << "Target " << target << " not found" << std::endl;
    }
    std::cout << "Parallel search with cancellation time: " << cancellation_duration.count() << " us\n";
    
    // Performance comparison
    std::cout << "\nPerformance Summary:\n";
    std::cout << "Serial search:                   " << serial_duration.count() << " us\n";
    std::cout << "Parallel search (invoke):       " << parallel_invoke_duration.count() << " us\n";
    std::cout << "Parallel search (basic):         " << parallel_duration.count() << " us\n";
    std::cout << "Parallel search (cancellation):  " << cancellation_duration.count() << " us\n";
    
    if (serial_duration.count() > 0) {
        double speedup_invoke = static_cast<double>(serial_duration.count()) / parallel_invoke_duration.count();
        double speedup_basic = static_cast<double>(serial_duration.count()) / parallel_duration.count();
        double speedup_cancel = static_cast<double>(serial_duration.count()) / cancellation_duration.count();
         std::cout << "Speedup (invoke):                " << speedup_invoke << "x\n";
        std::cout << "Speedup (basic):                 " << speedup_basic << "x\n";
        std::cout << "Speedup (cancellation):          " << speedup_cancel << "x\n";
    }
    
    // Clean up the tree
    delete root;
    
    return 0;
}
