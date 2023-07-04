/*
    Copyright (C) 2005-2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#define VIDEO_WINMAIN_ARGS

#include <iostream>

#include "omp.h"
#include "oneapi/tbb/tick_count.h"
#include "oneapi/tbb/global_control.h"

#include "common/utility/utility.hpp"
#include "common/utility/get_default_num_threads.hpp"

#include "seismic_video.hpp"
#include "universe.hpp"

Universe u;

struct RunOptions {
    //! It is used for console mode for test with different number of threads and also has
    //! meaning for GUI: threads.first  - use separate event/updating loop thread (>0) or not (0).
    //!                  threads.second - initialization value for scheduler
    utility::thread_number_range threads;
    int numberOfFrames;
    std::string stress_runtime;
    std::string velocity_runtime;
    bool silent;
    bool parallel;
    RunOptions(utility::thread_number_range threads_,
               int number_of_frames_,
               std::string stress_runtime_,
               std::string velocity_runtime_,
               bool silent_,
               bool parallel_)
            : threads(threads_),
              numberOfFrames(number_of_frames_),
              stress_runtime(stress_runtime_),
              velocity_runtime(velocity_runtime_),
              silent(silent_),
              parallel(parallel_) {}
};

RunOptions ParseCommandLine(int argc, char *argv[]) {
    // zero number of threads means to run serial version
    utility::thread_number_range threads(
        utility::get_default_num_threads, 0, utility::get_default_num_threads());

    int numberOfFrames = 0;
    bool silent = false;
    bool serial = false;
    std::string stress_runtime{"tbb"};
    std::string velocity_runtime{"tbb"};

    utility::parse_cli_arguments(
        argc,
        argv,
        utility::cli_argument_pack()
            //"-h" option for displaying help is present implicitly
            .positional_arg(threads, "n-of-threads", utility::thread_number_range_desc)
            .positional_arg(numberOfFrames,
                            "n-of-frames",
                            "number of frames the example processes internally (0 means unlimited)")
            .positional_arg(stress_runtime, "stress-runtime",
                            "The name of the runtime that will execute UpdateStress - [tbb,openmp]")
            .positional_arg(velocity_runtime, "velocity-runtime",
                            "The name of the runtime that will execute UpdateVelocity - [tbb,openmp]")
            .arg(silent, "silent", "no output except elapsed time")
            .arg(serial, "serial", "in GUI mode start with serial version of algorithm"));
    return RunOptions(threads, numberOfFrames, stress_runtime, velocity_runtime, silent, !serial);
}

int main(int argc, char *argv[]) {
    oneapi::tbb::tick_count mainStartTime = oneapi::tbb::tick_count::now();
    RunOptions options = ParseCommandLine(argc, argv);
    SeismicVideo video(u, options.numberOfFrames, options.threads.last, options.parallel);

    u.SetRuntimeForStress(options.stress_runtime);
    u.SetRuntimeForVelocity(options.velocity_runtime);

    // video layer init
    if (video.init_window(u.UniverseWidth, u.UniverseHeight)) {
        video.calc_fps = true;
        video.threaded = options.threads.first > 0;
        // video is ok, init Universe
        u.InitializeUniverse(video);
        // main loop
        video.main_loop();
    }
    else if (video.init_console()) {
        // do console mode
        if (options.numberOfFrames == 0) {
            options.numberOfFrames = 1000;
            std::cout << "Substituting 1000 for unlimited frames because not running interactively"
                      << "\n";
        }
        for (int p = options.threads.first; p <= options.threads.last;
             p = options.threads.step(p)) {
            oneapi::tbb::tick_count xwayParallelismStartTime = oneapi::tbb::tick_count::now();
            u.InitializeUniverse(video);
            int numberOfFrames = options.numberOfFrames;

            if (p == 0) {
                //run a serial version
                for (int i = 0; i < numberOfFrames; ++i) {
                    u.SerialUpdateUniverse();
                }
            }
            else {
                // Limit both runtimes before execution
                oneapi::tbb::global_control c(oneapi::tbb::global_control::max_allowed_parallelism, p);
                omp_set_num_threads(p);

                for (int i = 0; i < numberOfFrames; ++i) {
                    u.ParallelUpdateUniverse();
                }
            }

            if (!options.silent) {
                double fps =
                    options.numberOfFrames /
                    ((oneapi::tbb::tick_count::now() - xwayParallelismStartTime).seconds());
                std::cout << fps << " frame per sec with ";
                if (p == 0) {
                    std::cout << "serial code"
                              << "\n";
                }
                else {
                    std::cout << p << " way parallelism"
                              << "\n";
                }
            }
        }
    }
    video.terminate();
    utility::report_elapsed_time((oneapi::tbb::tick_count::now() - mainStartTime).seconds());
    return 0;
}
