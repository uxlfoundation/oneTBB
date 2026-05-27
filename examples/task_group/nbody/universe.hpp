#ifndef __TBB_examples_nbody_universe_HPP
#define __TBB_examples_nbody_universe_HPP

#include <cstdint>

#include "bodies.hpp"
#include "common/gui/video.hpp"

class Universe {
private:
    NBodies m_bodies;
    const double m_dt;
    const double m_eps2;

    // World coordinate bounds.
    double m_world_xmin = -1.2, m_world_xmax = 1.2;
    double m_world_ymin = -1.2, m_world_ymax = 1.2;

    drawing_memory m_dmem;
    color_t m_bg_color = 0;

    void serial_zero_forces();
    void serial_compute_forces();
    void serial_integrate();

    void parallel_zero_forces();
    void parallel_compute_forces();
    void parallel_integrate();

    // Convert world (physics) coordinates to pixel coordinates.
    int world_to_px_x(double wx, int px_w) const;
    int world_to_px_y(double wy) const;
public:
    enum { UniverseWidth = 1024, UniverseHeight = 768 };

    Universe(std::size_t n_bodies, double dt, double eps2);

    void initialize(const video& colorizer, std::uint32_t seed,
                    double moon_fraction, int n_planets);

    void serial_update();
    void parallel_update();

    void draw(int px_x0, int px_w);

    void set_drawing_memory(const drawing_memory& dmem) { m_dmem = dmem; }
};

#endif // __TBB_examples_nbody_universe_HPP
