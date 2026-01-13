#include <iostream>

import tbb;

int main() {
    tbb::parallel_for(0, 20,
        [](int value) {
            std::cout << "Body\n";
        });
}
