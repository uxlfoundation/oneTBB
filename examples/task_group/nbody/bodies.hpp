#ifndef __TBB_examples_nbody_bodies_HPP
#define __TBB_examples_nbody_bodies_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

constexpr double G = 1.0;
constexpr double DefaultEps2 = 1e-6;
constexpr double DefaultDt = 5e-5;
constexpr int DefaultThreshold = 64;

struct NBodies {
    using physics_array_type = std::unique_ptr<double[]>;
    using color_array_type = std::unique_ptr<std::uint32_t[]>;
    using radius_array_type = std::unique_ptr<int[]>;

    std::size_t count = 0;

    // Physics state arrays.
    physics_array_type x_position;
    physics_array_type y_position;

    physics_array_type x_velocity;
    physics_array_type y_velocity;

    physics_array_type x_force;
    physics_array_type y_force;

    physics_array_type mass;

    color_array_type color;
    radius_array_type radius;

    NBodies(std::size_t c)
        : count(c)
        , x_position(new double[count]())
        , y_position(new double[count]())
        , x_velocity(new double[count]())
        , y_velocity(new double[count]())
        , x_force(new double[count]())
        , y_force(new double[count]())
        , mass(new double[count]())
        , color(new std::uint32_t[count]())
        , radius(new int[count]())
    {}

    ~NBodies() = default;
};
#endif // __TBB_examples_nbody_bodies_HPP
