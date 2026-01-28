/*
    Copyright (C) 2005-2025 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "common/utility/get_default_num_threads.hpp"
#define VIDEO_WINMAIN_ARGS

#include <cstdio>

#include <iostream>

#include "oneapi/tbb/tick_count.h"

#include "common/utility/utility.hpp"

#include "fractal.hpp"
#include "fractal_video.hpp"

bool silent = false;
bool single = false;
bool schedule_auto = false;
int grain_size = 1;

std::string runtime = "tbb";

int main(int argc, char *argv[]) {
    oneapi::tbb::tick_count mainStartTime = oneapi::tbb::tick_count::now();

    // It is used for console mode for test with different number of threads and also has
    // meaning for GUI: threads.first  - use separate event/updating loop thread (>0) or not (0).
    //                  threads.second - initialization value for scheduler
    utility::thread_number_range threads(utility::get_default_num_threads);
    int num_frames = -1;
    int max_iterations = 1000000;

    // command line parsing
    utility::parse_cli_arguments(
        argc,
        argv,
        utility::cli_argument_pack()
            //"-h" option for displaying help is present implicitly
            // TODO: Consider setting number of threads per runtime and handling `auto` case separately.
            .positional_arg(threads, "n-of-threads", utility::thread_number_range_desc)
            .positional_arg(
                num_frames, "n-of-frames", "number of frames the example processes internally")
            .positional_arg(
                max_iterations, "max-of-iterations", "maximum number of the fractal iterations")
            .positional_arg(grain_size, "grain-size", "the grain size value")
            .arg(schedule_auto, "use-auto-partitioner", "use oneapi::tbb::auto_partitioner")
            .arg(silent, "silent", "no output except elapsed time")
            .arg(single, "single", "process only one fractal")
            .arg(runtime, "threading-runtime",
                 "the name of the runtime that will execute fractal - [tbb, openmp, mix]"));

    fractal_video video;

    // video layer init
    if (video.init_window(3840, 2160)) {
        video.calc_fps = false;
        video.threaded = threads.first > 0;
        // initialize fractal group
        fractal_group fg(video.get_drawing_memory(), threads.last, max_iterations, num_frames);
        video.set_fractal_group(fg);
        // main loop
        video.main_loop();
    }
    else if (video.init_console()) {
        // in console mode we always have limited number of frames
        num_frames = num_frames < 0 ? 1 : num_frames;
        fractal_group fg(video.get_drawing_memory(), utility::get_default_num_threads(), max_iterations, num_frames);
        for (int p = threads.first; p <= threads.last; p = threads.step(p)) {
            if (!silent) {
                printf("Threads = %d\n", p);
            }
            fg.run(p, !single);
        }
    }
    video.terminate();
    utility::report_elapsed_time((oneapi::tbb::tick_count::now() - mainStartTime).seconds());
    return 0;
}
