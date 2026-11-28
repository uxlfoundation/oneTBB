#define TBB_PREVIEW_TASK_GROUP_EXTENSIONS 1
#define _USE_MATH_DEFINES
#define NOMINMAX
#include "universe.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <limits>

#include "common/utility/fast_random.hpp"

#include <oneapi/tbb/task_group.h>

constexpr std::size_t serial_threshold = 64;

void apply_forces(std::size_t i, std::size_t j, NBodies& b, double eps2) {
    double dx = b.x_position[j] - b.x_position[i];
    double dy = b.y_position[j] - b.y_position[i];
    double r2 = dx * dx + dy * dy + eps2;

    double inv = 1.0 / (r2 * std::sqrt(r2));
    double s = G * b.mass[i] * b.mass[j] * inv;

    double fx = s * dx;
    double fy = s * dy;

    b.x_force[i] += fx;
    b.y_force[i] += fy;

    b.x_force[j] -= fx;
    b.y_force[j] -= fy;
}

void serial_triangle_forces(std::size_t n0, std::size_t n1, NBodies& b, double eps2) {
    for (std::size_t i = n0; i < n1; ++i) {
        for (std::size_t j = i + 1; j < n1; ++j) {
            apply_forces(i, j, b, eps2);
        }
    }
}

void serial_rectangle_forces(std::size_t i0, std::size_t i1, std::size_t j0, std::size_t j1, NBodies& b, double eps2) {
    for (std::size_t i = i0; i < i1; ++i) {
        for (std::size_t j = j0; j < j1; ++j) {
            apply_forces(i, j, b, eps2);
        }
    }
}

oneapi::tbb::task_handle n_body_parallel_rectangle(oneapi::tbb::task_group& tg, std::size_t i0, std::size_t i1,
                                                   std::size_t j0, std::size_t j1, NBodies& bodies, double eps2) {
    std::size_t di = i1 - i0;
    std::size_t dj = j1 - j0;
    oneapi::tbb::task_handle next_task;

    if (di <= serial_threshold || dj <= serial_threshold) {
        serial_rectangle_forces(i0, i1, j0, j1, bodies, eps2);
    } else {
        std::size_t im = i0 + di / 2;
        std::size_t jm = j0 + dj / 2;

        oneapi::tbb::task_handle left_lower_rectangle = tg.defer([=, &tg, &bodies] {
            return n_body_parallel_rectangle(tg, i0, im, j0, jm, bodies, eps2);
        });

        oneapi::tbb::task_handle right_upper_rectangle = tg.defer([=, &tg, &bodies] {
            return n_body_parallel_rectangle(tg, im, i1, jm, j1, bodies, eps2);
        });

        oneapi::tbb::task_handle left_upper_rectangle = tg.defer([=, &tg, &bodies] {
            return n_body_parallel_rectangle(tg, i0, im, jm, j1, bodies, eps2);
        });

        oneapi::tbb::task_handle right_lower_rectangle = tg.defer([=, &tg, &bodies] {
            return n_body_parallel_rectangle(tg, im, i1, j0, jm, bodies, eps2);
        });

        oneapi::tbb::task_handle empty_sync = tg.defer([] {});

        oneapi::tbb::task_group::set_task_order(left_lower_rectangle,  left_upper_rectangle);
        oneapi::tbb::task_group::set_task_order(left_lower_rectangle,  right_lower_rectangle);
        oneapi::tbb::task_group::set_task_order(right_upper_rectangle, left_upper_rectangle);
        oneapi::tbb::task_group::set_task_order(right_upper_rectangle, right_lower_rectangle);

        oneapi::tbb::task_group::set_task_order(left_upper_rectangle, empty_sync);
        oneapi::tbb::task_group::set_task_order(right_lower_rectangle, empty_sync);

        oneapi::tbb::task_group::transfer_this_task_completion_to(empty_sync);

        tg.run(std::move(right_upper_rectangle));
        tg.run(std::move(left_upper_rectangle));
        tg.run(std::move(right_lower_rectangle));
        tg.run(std::move(empty_sync));
        next_task = std::move(left_lower_rectangle);
    }
    return std::move(next_task);
}

oneapi::tbb::task_handle n_body_parallel_triangle(oneapi::tbb::task_group& tg, std::size_t n0, std::size_t n1,
                                                  NBodies& bodies, double eps2) {
    std::size_t dn = n1 - n0;
    oneapi::tbb::task_handle next_task;

    if (dn <= serial_threshold) {
        serial_triangle_forces(n0, n1, bodies, eps2);
    } else {
        std::size_t nm = n0 + dn / 2;

        oneapi::tbb::task_handle triangle1 = tg.defer([=, &tg, &bodies] {
            return n_body_parallel_triangle(tg, n0, nm, bodies, eps2);
        });

        oneapi::tbb::task_handle triangle2 = tg.defer([=, &tg, &bodies] {
            return n_body_parallel_triangle(tg, nm, n1, bodies, eps2);
        });

        oneapi::tbb::task_handle rectangle = tg.defer([=, &tg, &bodies] {
            return n_body_parallel_rectangle(tg, n0, nm, nm, n1, bodies, eps2);
        });

        oneapi::tbb::task_group::set_task_order(triangle1, rectangle);
        oneapi::tbb::task_group::set_task_order(triangle2, rectangle);

        oneapi::tbb::task_group::transfer_this_task_completion_to(rectangle);

        tg.run(std::move(triangle2));
        tg.run(std::move(rectangle));

        next_task = std::move(triangle1);
    }
    return std::move(next_task);
}

Universe::Universe(std::size_t n_bodies, double dt, double eps2)
    : m_bodies(n_bodies)
    , m_dt(dt)
    , m_eps2(eps2)
{}

void Universe::initialize(const video& colorizer, std::uint32_t seed,
                          double moon_fraction, int n_planets) {
    utility::FastRandom rng(seed);

    auto& b = m_bodies;

    // A generator of uniformly distributed doubles in [0, 1].
    const double inv_max = 1.0 / double(std::numeric_limits<unsigned short>::max());
    auto urand = [&]() { return double(rng.get()) * inv_max; };

    m_bg_color = colorizer.get_color(0, 0, 0); // Black

    // Place the Sun in the center
    const double sun_mass = 1000.0;
    b.x_position[0] = b.y_position[0] = 0.0;
    b.x_velocity[0] = b.y_velocity[0] = 0.0;
    b.mass[0]   = sun_mass;
    b.color[0]  = colorizer.get_color(255, 220, 80); // Yellow
    b.radius[0] = 10;

    struct PlanetDesc {
        double        mass;
        std::size_t   moons;
        unsigned char r, g, b;
    };

    const PlanetDesc planet_palette[] = {
        { 30.0, 2, 100, 160, 255 }, // Blue
        { 22.0, 3, 220, 120,  80 }, // Red
        { 16.0, 2, 120, 220, 140 }, // Green
        { 12.0, 3, 230, 200, 120 }, // Tan
        {  9.0, 1, 180, 140, 240 }, // Purple
        {  7.0, 4, 240, 240, 240 }, // White
    };
    constexpr std::size_t palette_size =
        sizeof(planet_palette) / sizeof(planet_palette[0]);

    std::size_t want_planets;
    if (n_planets > 0) {
        want_planets = std::size_t(n_planets);
    } else {
        // n_planets == 0 is "auto": scale to body count.
        want_planets = b.count > 1000 ? palette_size
                     : b.count > 200  ? 4
                     : b.count > 50   ? 2
                     : 0;
    }

    // Radial band where planets are allowed to be placed.
    const double planet_r_min = 0.45;
    const double planet_r_max = 0.85;

    struct Planet {
        double orbit_radius;
        double mass;
        double px, py;
        double vx, vy;
        double hill_radius;
        color_t color;
    };

    std::size_t next = 1;

    std::vector<Planet> planets;
    planets.reserve(want_planets);

    // Create planets, round-robin through planet_palette.
    for (std::size_t p = 0; p < want_planets; ++p) {
        const PlanetDesc& d = planet_palette[p % palette_size];

        // Distribute planet radius between planet_r_min and planet_r_max.
        double band_fraction = want_planets == 1 ? 0.5
                                    : double (p) / double(want_planets - 1);
        double r = planet_r_min + (planet_r_max - planet_r_min) * band_fraction;

        // Compute initial position and velocity.
        double theta = 2.0 * M_PI * double(p) / double(want_planets);
        double cs = std::cos(theta), sn = std::sin(theta);
        double v = std::sqrt(sun_mass / r); // orbital speed

        std::size_t i = next++;
        b.x_position[i] = r * cs;
        b.y_position[i] = r * sn;
        b.x_velocity[i] = -v * sn;
        b.y_velocity[i] =  v * cs;
        b.mass[i]   = d.mass;
        b.color[i]  = colorizer.get_color(d.r, d.g, d.b);
        b.radius[i] = 3;

        // Planet Hill radius = orbit_radius * (mass / (3 * (mass + sun_mass)))^(1/3).
        // Drop mass from the denominator since it is much smaller than sun_mass.
        double hill = r * std::cbrt(d.mass / (3.0 * sun_mass));

        planets.push_back({ r, d.mass,
                            b.x_position[i], b.y_position[i],
                            b.x_velocity[i], b.y_velocity[i],
                            hill,
                            b.color[i] });
    }

    auto place_moon = [&](const Planet& pl, std::size_t mi) {
        // Radial band for the moon: 20% to 45% of the planet's Hill sphere.
        double moon_orbit_radius_min = 0.20 * pl.hill_radius;
        double moon_orbit_radius_max = 0.45 * pl.hill_radius;

        // Pick a random angle and radius inside the radial band.
        double moon_orbit_angle = urand() * 2.0 * M_PI;
        double moon_orbit_radius = moon_orbit_radius_min +
                                   urand() * (moon_orbit_radius_max - moon_orbit_radius_min);

        double cs = std::cos(moon_orbit_angle), sn = std::sin(moon_orbit_angle);

        // Circular orbit speed around the planet, using the softened force law:
        // v = sqrt(G * planet_mass * r^2 / (r^2 + eps^2)^(3/2)).
        double r2_soft = moon_orbit_radius * moon_orbit_radius + m_eps2;
        double orbit_speed = std::sqrt(pl.mass * moon_orbit_radius * moon_orbit_radius /
                                       (r2_soft * std::sqrt(r2_soft)));

        b.x_position[mi] = pl.px + moon_orbit_radius * cs;
        b.y_position[mi] = pl.py + moon_orbit_radius * sn;
        b.x_velocity[mi] = pl.vx + (-orbit_speed * sn);
        b.y_velocity[mi] = pl.vy +  (orbit_speed * cs);

        b.mass[mi]   = 1e-4;
        b.color[mi]  = colorizer.get_color(255, 255, 255); // White
        b.radius[mi] = 0; // Single pixel.
    };

    // Baseline moons from the palette.
    for (std::size_t p = 0; p < want_planets && next < b.count; ++p) {
        for (int m = 0; m < planet_palette[p % palette_size].moons && next < b.count; ++m) {
            place_moon(planets[p], next++);
        }
    }

    // Extra moons from --moon-fraction, taking them from the remaining dust
    // budget and round-robin across planets.
    std::size_t remaining = b.count - next;
    std::size_t extra = std::size_t(double(remaining) * moon_fraction);
    for (std::size_t k = 0; k < extra && next < b.count; ++k) {
        place_moon(planets[k % want_planets], next++);
    }

    // Place dust within this radial band.
    const double dust_r_min = 0.15;
    const double dust_r_max = 0.90;
    for (std::size_t i = next; i < b.count; ++i) {
        double r = dust_r_min + urand() * (dust_r_max - dust_r_min);
        double theta = urand() * 2.0 * M_PI;
        double cs = std::cos(theta), sn = std::sin(theta);
        double v = std::sqrt(sun_mass / r);

        b.x_position[i] = r * cs;
        b.y_position[i] = r * sn;
        b.x_velocity[i] = -v * sn;
        b.y_velocity[i] =  v * cs;
        b.mass[i]   = 1e-5 + urand() * 1e-4;

        b.color[i]  = colorizer.get_color(90, 90, 110); // Grey
        b.radius[i] = 0; // Single pixel.
    }

    // Momentum balance: shift the system into the center-of-momentum frame so
    // that the total momentum of the system is zero.
    {
        double total_mass = 0.0;
        double px_sum = 0.0, py_sum = 0.0;
        for (std::size_t i = 0; i < b.count; ++i) {
            total_mass += b.mass[i];
            px_sum += b.mass[i] * b.x_velocity[i];
            py_sum += b.mass[i] * b.y_velocity[i];
        }
        double vx_shift = px_sum / total_mass;
        double vy_shift = py_sum / total_mass;
        for (std::size_t i = 0; i < b.count; ++i) {
            b.x_velocity[i] -= vx_shift;
            b.y_velocity[i] -= vy_shift;
        }
    }

    m_world_xmin = m_world_ymin = -1.0;
    m_world_xmax = m_world_ymax = 1.0;
}

void Universe::serial_zero_forces() {
    auto& b = m_bodies;

    for (std::size_t i = 0; i < b.count; ++i) {
        b.x_force[i] = 0.0;
        b.y_force[i] = 0.0;
    }
}

void Universe::serial_compute_forces() {
    serial_triangle_forces(0, m_bodies.count, m_bodies, m_eps2);
}

void Universe::integrate_bodies(std::size_t begin, std::size_t end) {
    auto& b = m_bodies;

    const double dt = m_dt;

    for (std::size_t i = begin; i < end; ++i) {
        double ax = b.x_force[i] / b.mass[i];
        double ay = b.y_force[i] / b.mass[i];

        b.x_velocity[i] += ax * dt;
        b.y_velocity[i] += ay * dt;

        b.x_position[i] += b.x_velocity[i] * dt;
        b.y_position[i] += b.y_velocity[i] * dt;
    }
}

void Universe::serial_integrate() {
    integrate_bodies(0, m_bodies.count);
}

void Universe::parallel_integrate() {
    auto& b = m_bodies;

    tbb::task_group tg;
    std::size_t i = 0;

    while (i + serial_threshold < b.count) {
        tg.run([&, i]() {
            integrate_bodies(i, i + serial_threshold);
        });
        i += serial_threshold;
    }

    integrate_bodies(i, b.count);
    tg.wait();
}

void Universe::serial_update() {
    serial_zero_forces();
    serial_compute_forces();
    serial_integrate();
}

void Universe::parallel_compute_forces() {
    oneapi::tbb::task_group tg;

    tg.run_and_wait([&tg, this] {
        return n_body_parallel_triangle(tg, 0, m_bodies.count, m_bodies, m_eps2);
    });
}

void Universe::parallel_update() {
    serial_zero_forces();
    parallel_compute_forces();
    parallel_integrate();
}

int Universe::world_to_px_x(double wx, int px_w) const {
    return int((wx - m_world_xmin) / (m_world_xmax - m_world_xmin) * px_w);
}

int Universe::world_to_px_y(double wy) const {
    return int((wy - m_world_ymin) / (m_world_ymax - m_world_ymin) * UniverseHeight);
}

void Universe::draw(int px_x0, int px_w) {
    drawing_area da(px_x0, 0, px_w, UniverseHeight, m_dmem);

    // Clear the background
    for (int y = 0; y < UniverseHeight; ++y) {
        da.set_pos(0, y);
        for (int x = 0; x < px_w; ++x) {
            da.put_pixel(m_bg_color);
        }
    }

    auto& b = m_bodies;

    // Draw each body
    for (std::size_t i = 0; i < b.count; ++i) {
        int px = world_to_px_x(b.x_position[i], px_w);
        int py = world_to_px_y(b.y_position[i]);
        int rr = b.radius[i];
        color_t c = color_t(b.color[i]);

        if (rr == 0) {
            // Draw single-pixel body (moon or dust)
            if (px >= 0 && px < px_w && py >= 0 && py < UniverseHeight) {
                da.set_pos(px, py);
                da.put_pixel(c);
            }
        } else {
            // Draw filled disc of radius rr
            const int r2 = rr * rr;
            for (int dy = -rr; dy <= rr; ++dy) {
                int y = py + dy;
                if (y < 0 || y >= UniverseHeight) continue;
                for (int dx = -rr; dx <= rr; ++dx) {
                    int x = px + dx;
                    if (x < 0 || x >= px_w) continue;
                    if (dx * dx + dy * dy > r2) continue;
                    da.set_pos(x, y);
                    da.put_pixel(c);
                }
            }
        }
    }

    da.update();
}
