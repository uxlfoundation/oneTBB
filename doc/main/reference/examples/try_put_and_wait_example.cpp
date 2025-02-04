#include <tuple>

// dummy bodies
struct f1_body {
    int operator()(int input) { return input; };
};

struct f2_body : f1_body {};
struct f3_body : f1_body {};

struct f4_body {
    int operator()(const std::tuple<int, int>& input) {
        return 0;
    }
};

#include "try_put_and_wait_example.h"
