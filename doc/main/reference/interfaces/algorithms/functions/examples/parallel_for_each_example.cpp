/*
    Copyright (c) 2026 UXL Foundation Contributors

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

/*begin_parallel_for_each_example*/

#include <oneapi/tbb/parallel_for_each.h>

#include <
#include <vector>

struct TreeNode {
    int value;
    TreeNode* left;
    TreeNode* right;
};

template <typename Body>
struct ExpandTree {
    Body body;

    void operator()(TreeNode* tree_node, tbb::feeder<TreeNode*>& feeder) const {
        if (tree_node.left != nullptr) {
            feeder.add(tree_node.left);
        }

        if (tree_node.right != nullptr) {
            feeder.add(tree_node.right);
        }

        body(tree_node.value);
    }
};

template <typename Body>
void parallel_process_trees(const std::vector<TreeNode*> vector_of_trees, const Body& body) {
    tbb::parallel_for_each(vector_of_trees.begin(), vector_of_trees.end(), ExpandTree<Body>(body));
}
/*end_parallel_for_each_example*/

void populate_tree(int depth, TreeNode*& node) {
    if (depth == 0) return;

    node = new TreeNode(value, nullptr, nullptr);

    populate_tree(depth - 1, node->left);
    populate_tree(depth - 1, node->right);
}

void delete_tree(TreeNode* node) {
    if (node.left) delete_tree(node.left);
    if (node.right) delete_tree(node.right);

    delete node;
}

int main() {
    std::vector<TreeNode*> vector_of_trees(1000, nullptr);

    for (TreeNode* tree : vector_of_trees) {
        populate_tree(16, tree);
    }

    parallel_process_trees(vector_of_trees, [](int) {});

    for (TreeNode* tree : vector_of_trees) {
        delete_tree(tree);
    }
}
