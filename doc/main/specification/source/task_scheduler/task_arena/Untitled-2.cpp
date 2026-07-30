namespace tbb {

class task_handle;
class task_completion_handle;

namespace task {



} // namespace task

} // namespace tbb

void parallel_fibonacci(int n, int* result) {
    int* left = new int(0);
    int* right = new int(0);

    tbb::task_handle left = tbb::task::create([=] {
        parallel_fibonacci(n - 1, left);
    });
    tbb::task_handle right = tbb::task::create([=] {
        parallel_fibonacci(n - 2, right);
    });
    tbb::task_handle merge = tbb::task::create([=] {
        *result = *left + *right;
        delete left;
        delete right;
    });

    tbb::task::set_order(left, merge);
    tbb::task::set_order(right, merge);
    tbb::task::transfer_this_task_completion_to(merge); // Can add to the group of this_task!!!

    tbb::task::run(std::move(left));
    tbb::task::run(std::move(right));
    tbb::task::run(std::move(merge));
}

int main() {
    int n = 1000;
    int result = 0;
    tbb::task_group tg;

    tg.run_and_wait([n, &result] { parallel_fibonacci(n, &result); });
}