/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#pragma once

#include <chrono>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <random>
#include <string>
#include <vector>

#include "executors.h"

struct duration_logger {
    std::ofstream output;
    std::string bench_name;
    std::size_t data_size;
    std::size_t iterations;
    std::size_t hw_concurrency;
    std::size_t max_concurrency;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
    std::size_t duration;
    std::string concurrency_chain {"N/A"};
    bool print_to_stdout;

    duration_logger(const std::string& bench, std::size_t hw, bool to_print=false) : bench_name(bench), hw_concurrency(hw), print_to_stdout(to_print) {
        output.open(bench + "_result.csv");
        std::ostringstream out;
        out << "benchmark,chain,hw_concurrency,max_concurrency,concurrency_chain,iterations,data_size,duration (ns)" << std::endl;
        dump_impl(out);
    }

    void start() {
        start_time = std::chrono::high_resolution_clock::now();
    }

    void stop() {
        duration = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
    }

    void dump(const std::string& chain_name) {
        std::ostringstream out;
        out << bench_name << ',' << chain_name << ',' << hw_concurrency << ','
            << max_concurrency << ',' << concurrency_chain << ',' << iterations << ',' << data_size << ',' << duration << std::endl;
        dump_impl(out);
    }
private:
    void dump_impl(std::ostringstream& printable) {
        output << printable.str();
        if (print_to_stdout) {
            std::cout << printable.str();
        }
    }
};

struct argument_parser {
    std::vector<std::size_t> work_sizes;
    bool print_to_stdout {false};
    

    argument_parser() = default;
    argument_parser(int argc, char** argv) { parse_arguments(argc, argv); };
    
    void parse_arguments(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string argument { argv[i] };
            if (argument == "--help" || argument == "-h") {
                print_help(argv[0]);
                std::exit(0);
            } else if (argument == "-v" || argument == "--verbose") {
                print_to_stdout = true;
            } else if (argument == "--data-sizes") {
                parse_data_sizes(i, argc, argv);
            }
        }
    }
private:
    void print_help(const char* executable) {
        constexpr std::size_t width = 24;
        std::cout << 
        "Usage: " << executable << " [OPTION]...\n\n" <<
        "List of options:\n" <<
        std::left << std::setw(width) << "-h, --help" << "display this help and exit\n" <<
        std::left << std::setw(width) << "--data-sizes LIST" << "comma-separated list of data sizes\n" <<
        std::left << std::setw(width) << "-v, --verbose" << "print benchmark results to stdout\n" << std::endl;
    }

    void parse_data_sizes(int pos, int argc, char** argv) {
        if (pos+1 >= argc) {
            throw std::invalid_argument{"Please specify value for --data-sizes (Use --help for more info)"};
        }
        std::string values = {argv[pos+1]};
        std::size_t start = 0;
        std::size_t end = values.find(',', start);
        while(end != std::string::npos) {
            std::string token{values.substr(start, end-start)};
            work_sizes.push_back(std::stoull(token));

            start = end+1;
            end = values.find(',', start);
        }
        work_sizes.push_back(std::stoull(values.substr(start, end-start)));
    }
};

template <typename Array, typename Expected>
bool check_sequence(Array&& array, std::size_t size, Expected&& expected, const std::string& message) {
    for (std::size_t i = 0; i < size; ++i) {
        if (array[i] != expected) {
            std::cerr << "Verification failed for " <<  message << ":"  << std::endl;
            std::cerr << "i = " << i << std::endl;
            std::cerr << "got: " << array[i] << "; expected: " << expected << std::endl;
            return 1;
        }
    }
    return 0;
}

template <typename Sequence, typename Func, typename Comp>
bool check_sequence_analytically(std::size_t size, Sequence&& init, Sequence&& res, Func&& func, Comp&& comp, const std::string& message) {
    for (std::size_t i = 0; i < size; ++i) {
        if (!comp(res[i], func(init[i]))) {
            std::cerr << "Verification failed for " <<  message << ":"  << std::endl;
            std::cerr << "i = " << i << std::endl;
            std::cerr << "got: " << res[i] << "; expected: " << func(init[i]) << std::endl;
            return 1;
        }
    }
    return 0;
}

template <typename Sequence, typename Func, typename Comp>
bool check_sequence_analytically(std::size_t size, Sequence&& init1, Sequence&& init2, Sequence&& res, Func&& func, Comp&& comp, const std::string& message) {
    for (std::size_t i = 0; i < size; ++i)
    {
        if (!comp(res[i], func(init1[i], init2[i])))
        {
            std::cerr << "Verification failed for " <<  message << ":"  << std::endl;
            std::cerr << "i = " << i << std::endl;
            std::cerr << "got: " << res[i] << "; expected: " << func(init1[i], init2[i]) << std::endl;
            return 1;
        }
    }
    return 0;
}

template <typename Type>
void generate_random_data(unsigned seed, Type l_border, Type r_border, std::vector<Type>& vec) {
    std::mt19937 engine{seed};
    std::uniform_real_distribution<> distribution{l_border, r_border};
    for (auto& elem : vec)
      elem = Type(distribution(engine)); 
}
