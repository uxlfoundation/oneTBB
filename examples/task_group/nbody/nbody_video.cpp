#include "nbody_video.hpp"
#include "universe.hpp"

#include <chrono>
#include <cstdio>

NBodyVideo::NBodyVideo(Universe& left, Universe& right, std::size_t num_frames)
    : m_left_universe(left)
    , m_right_universe(right)
    , m_num_frames(num_frames)
{
    std::snprintf(m_title_buf, sizeof(m_title_buf),
                  "N-Body: serial (left) vs parallel (right)");
    title = m_title_buf;
    calc_fps = false; // We manage the title ourselves
}

void NBodyVideo::on_process() {
    using clock = std::chrono::steady_clock;
    const int half = Universe::UniverseWidth / 2;
    const auto budget = std::chrono::milliseconds(33);

    for (std::size_t frame = 0; m_num_frames == 0 || frame < m_num_frames; ++frame) {
        // Run serial computations and count steps
        std::size_t serial_steps = 0;
        auto start = clock::now();
        auto end = start + budget;
        do {
            m_left_universe.serial_update();
            ++serial_steps;
        } while (clock::now() < end);
        double serial_elapsed_s = std::chrono::duration<double>(clock::now() - start).count();

        // Run parallel computations and count steps
        std::size_t parallel_steps = 0;
        start = clock::now();
        end = start + budget;
        do {
            m_right_universe.parallel_update();
            ++parallel_steps;
        } while (clock::now() < end);
        double parallel_elapsed_s = std::chrono::duration<double>(clock::now() - start).count();

        m_left_universe.draw(0, half);
        m_right_universe.draw(half, half);

        double serial_sps   = serial_steps   / serial_elapsed_s;
        double parallel_sps = parallel_steps / parallel_elapsed_s;
        std::snprintf(m_title_buf, sizeof(m_title_buf),
                      "N-Body: serial %.0f steps/s | parallel %.0f steps/s | speedup %.2fx",
                      serial_sps, parallel_sps,
                      serial_sps > 0.0 ? parallel_sps / serial_sps : 0.0);
        show_title();

        if (!next_frame()) break;
    }
}
