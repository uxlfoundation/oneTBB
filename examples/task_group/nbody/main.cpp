#define VIDEO_WINMAIN_ARGS

#include <cstddef>
#include <cstdint>
#include <iostream>

#include "oneapi/tbb/tick_count.h"

#include "common/utility/utility.hpp"

#include "bodies.hpp"
#include "universe.hpp"
#include "nbody_video.hpp"

struct RunOptions {
    std::size_t   n_bodies;
    std::size_t   n_planets;
    std::size_t   n_frames;
    std::uint32_t seed;
    double        dt;
    double        eps2;
    double        moon_fraction;
    bool          silent;
};

static bool parse_cli(int argc, char** argv, RunOptions& opts) {
    int n_bodies = 5000;
    int n_planets = 8;
    int n_frames = 0;

    double dt = DefaultDt;
    double eps2 = DefaultEps2;
    double moon_fraction = 0;

    int seed = 12345;

    bool silent = false;

    utility::parse_cli_arguments(argc, argv,
        utility::cli_argument_pack()
            .positional_arg(n_bodies, "--n-bodies", "Number of Bodies")
            .positional_arg(n_frames, "--n-frames", "Number of Frames (0 = unlimited)")
            .arg(n_planets, "--n-planets", "Number of Planets")
            .arg(dt, "--dt", "Integration timestep")
            .arg(eps2, "--eps2", "Softening squared")
            .arg(moon_fraction, "--moon-fraction",
                 "Fraction of dust bodies (0..1) to place as planetary Moons")
            .arg(seed, "--seed", "RNG seed for initial conditions")
            .arg(silent, "--silent", "Suppress textual output"));

    // Validate arguments.
    if (n_bodies < 0) {
        std::cerr << "ERROR: expected positive --n-bodies. " << n_bodies << " was passed.\n";
        return false;
    }
    if (n_frames < 0) {
        std::cerr << "ERROR: expected positive --n-frames. " << n_frames << " was passed.\n";
        return false;
    }
    if (n_planets < 0 || n_planets >= n_bodies) {
        std::cerr << "ERROR: --n-planets should be in a range [0, --n-bodies). " <<
                     n_planets << " was passed.\n";
        return false;
    }
    if (dt < 0) {
        std::cerr << "ERROR: expected positive --dt. " << dt << " was passed.\n";
        return false;
    }
    if (eps2 < 0) {
        std::cerr << "ERROR: expected positive --eps2. " << eps2 << " was passed\n";
        return false;
    }
    if (moon_fraction < 0 || moon_fraction > 1) {
        std::cerr << "ERROR: --moon-fraction should be in a range [0, 1]. " <<
                     moon_fraction << " was passed.\n";
        return false;
    }

    opts = {std::size_t(n_bodies), std::size_t(n_planets), std::size_t(n_frames),
            std::uint32_t(seed), dt, eps2, moon_fraction, silent};
    return true;
}

int main(int argc, char** argv) {
    oneapi::tbb::tick_count main_start = oneapi::tbb::tick_count::now();

    RunOptions opts;
    if (!parse_cli(argc, argv, opts)) return -1;

    Universe left_universe(opts.n_bodies, opts.dt, opts.eps2);
    Universe right_universe(opts.n_bodies, opts.dt, opts.eps2);
    NBodyVideo video(left_universe, right_universe, opts.n_frames);

    if (video.init_window(Universe::UniverseWidth, Universe::UniverseHeight)) {
        video.calc_fps = true;
        video.threaded = false;
        left_universe.initialize(video, opts.seed, opts.moon_fraction, opts.n_planets);
        right_universe.initialize(video, opts.seed, opts.moon_fraction, opts.n_planets);

        left_universe.set_drawing_memory(video.get_drawing_memory());
        right_universe.set_drawing_memory(video.get_drawing_memory());
        video.main_loop();
    } else if (video.init_console()) {
        int frames = opts.n_frames > 0 ? opts.n_frames : 1000;
        right_universe.initialize(video, opts.seed, opts.moon_fraction, opts.n_planets);
        oneapi::tbb::tick_count t0 = oneapi::tbb::tick_count::now();
        
        for (int i = 0; i < frames; ++i) left_universe.serial_update();

        oneapi::tbb::tick_count t1 = oneapi::tbb::tick_count::now();

        for (int i = 0; i < frames; ++i) right_universe.parallel_update();

        oneapi::tbb::tick_count t2 = oneapi::tbb::tick_count::now();

        double serial_secs = (t1 - t0).seconds();
        double parallel_secs = (t2 - t1).seconds();

        if (!opts.silent) {
            std::cout << "serial: " << frames << " frames in " << serial_secs << " s (" << (frames / serial_secs) << " fps)\n";
            std::cout << "parallel: " << frames << " frames in " << parallel_secs << " s (" << (frames / parallel_secs) << " fps)\n";
        }
    }

    video.terminate();
    oneapi::tbb::tick_count main_finish = oneapi::tbb::tick_count::now();

    utility::report_elapsed_time((main_finish - main_start).seconds());
    return 0;
}
