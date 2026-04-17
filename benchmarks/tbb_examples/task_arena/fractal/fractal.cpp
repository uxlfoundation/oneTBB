/*
   Copyright (C) 2005 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

*/

#include <cmath>
#include <cstdio>
#include <memory>

#include "oneapi/tbb/parallel_for.h"
#include "oneapi/tbb/blocked_range2d.h"
#include "oneapi/tbb/tick_count.h"

#include "omp.h"

#include "fractal.hpp"

video *v;
extern bool silent;
extern bool schedule_auto;
extern int grain_size;
extern std::string runtime;

color_t fractal::calc_one_pixel(int x0, int y0) const {
    unsigned int iter;
    double fx0, fy0, xtemp, x, y, mu;

    color_t color;

    fx0 = (double)x0 - (double)size_x / 2.0;
    fy0 = (double)y0 - (double)size_y / 2.0;
    fx0 = fx0 / magn + cx;
    fy0 = fy0 / magn + cy;

    iter = 0;
    x = 0;
    y = 0;
    mu = 0;

    while (((x * x + y * y) <= 4) && (iter < max_iterations)) {
        xtemp = x * x - y * y + fx0;
        y = 2 * x * y + fy0;
        x = xtemp;
        mu += exp(-sqrt(x * x + y * y));
        iter++;
    }

    if (iter == max_iterations) {
        // point corresponds to the mandelbrot set
        color = v->get_color(255, 255, 255);
        return color;
    }

    int b = (int)(256 * mu);
    int g = (b / 8);
    int r = (g / 16);

    b = b > 255 ? 255 : b;
    g = g > 255 ? 255 : g;
    r = r > 255 ? 255 : r;

    color = v->get_color(r, g, b);
    return color;
}

void fractal::clear() {
    drawing_area area(off_x, off_y, size_x, size_y, dm);

    // fill the rendering area with black color
    for (int y = 0; y < size_y; ++y) {
        area.set_pos(0, y);
        for (int x = 0; x < size_x; ++x) {
            area.put_pixel(v->get_color(0, 0, 0));
        }
    }
}

void fractal::draw_border(bool is_active) {
    color_t color = is_active ? v->get_color(0, 255, 0) // green color
                              : v->get_color(96, 128, 96); // green-gray color

    // top border
    drawing_area area0(off_x - 1, off_y - 1, size_x + 2, 1, dm);
    for (int i = -1; i < size_x + 1; ++i)
        area0.put_pixel(color);
    // bottom border
    drawing_area area1(off_x - 1, off_y + size_y, size_x + 2, 1, dm);
    for (int i = -1; i < size_x + 1; ++i)
        area1.put_pixel(color);
    // left border
    drawing_area area2(off_x - 1, off_y, 1, size_y + 2, dm);
    for (int i = 0; i < size_y; ++i)
        area2.set_pixel(0, i, color);
    // right border
    drawing_area area3(size_x + off_x, off_y, 1, size_y + 2, dm);
    for (int i = 0; i < size_y; ++i)
        area3.set_pixel(0, i, color);
}

void fractal::render_rect(int x0, int y0, int x1, int y1) const {
    // render the specified rectangle area
    drawing_area area(off_x + x0, off_y + y0, x1 - x0, y1 - y0, dm);
    for (int y = y0; y < y1; ++y) {
        area.set_pos(0, y - y0);
        for (int x = x0; x < x1; ++x) {
            area.put_pixel(calc_one_pixel(x, y));
        }
    }
}

bool fractal::check_point(int x, int y) const {
    return x >= off_x && x <= off_x + size_x && y >= off_y && y <= off_y + size_y;
}

class fractal_body {
    fractal &f;

public:
    void operator()(oneapi::tbb::blocked_range2d<int> &r) const {
        if (v->next_frame())
            f.render_rect(r.cols().begin(), r.rows().begin(), r.cols().end(), r.rows().end());
    }

    fractal_body(fractal &_f) : f(_f) {}
};

void fractal_runtime_tbb::render() {
    f.clear();
    context.reset();
    // Make copy of fractal object and render fractal with parallel_for with
    // the provided context and partitioner chosen by schedule_auto.
    // Updates to fractal are not reflected in the render.
    fractal f_copy = f;
    fractal_body body(f_copy);

    if (schedule_auto)
        oneapi::tbb::parallel_for(
            oneapi::tbb::blocked_range2d<int>(0, f.get_size_y(), grain_size, 0, f.get_size_x(), grain_size),
            body,
            oneapi::tbb::auto_partitioner(),
            context);
    else
        oneapi::tbb::parallel_for(
            oneapi::tbb::blocked_range2d<int>(0, f.get_size_y(), grain_size, 0, f.get_size_x(), grain_size),
            body,
            oneapi::tbb::simple_partitioner(),
            context);
}


void fractal_runtime_omp::render() {
    f.clear();
    // Make copy of fractal object and render fractal with parallel_for with
    // the provided context and partitioner chosen by schedule_auto.
    // Updates to fractal are not reflected in the render.
    fractal f_copy = f;

    // Since per thread load depends on pixel coordinates, to keep each thread busy dynamic
    // scheduling is chosen
    #pragma omp parallel for num_threads(nthreads) collapse(2) schedule(dynamic, grain_size)
    for (int y = 0; y < f.get_size_y(); ++y) {
        for (int x = 0; x < f.get_size_x(); ++x) {
            if (v->next_frame())
                f_copy.render_rect(x, y, x+1, y+1);
        }
    }
}

void fractal_runtime::calc_fractal() {
    // calculate the fractal
    oneapi::tbb::tick_count t0 = oneapi::tbb::tick_count::now();
    while (v->next_frame() && num_frames != 0) {
        render();
        if (num_frames > 0) {
            num_frames -= 1;
        }
    }
    oneapi::tbb::tick_count t1 = oneapi::tbb::tick_count::now();

    if (!silent) {
        printf("  %s fractal finished. Time: %g\n", name, (t1 - t0).seconds());
    }
}

void fractal_group::switch_active(int new_active) {
    if (new_active != -1)
        active = new_active;
    else
        active = 1 - active; // assumes 'active' is only 0 or 1
    draw_borders();
}

void fractal_group::set_num_frames_at_least(int n) {
    num_frames = num_frames < n ? n : num_frames;
}

void fractal_group::run(int nthreads, bool create_second_fractal) {
    // First argument of arenas constructor is used to restrict concurrency
    f_run[0]->initialize(nthreads);
    f_run[1]->initialize(nthreads);

    draw_borders();

    // the second fractal is calculating on separated thread
    if (create_second_fractal) {
        f_run[1]->run();
    }

    f_run[0]->run_and_wait();

    if (create_second_fractal) {
        f_run[1]->wait();
    }
}

void fractal_group::draw_borders() {
    f0.draw_border(active == 0);
    f1.draw_border(active == 1);
}

fractal_group::fractal_group(const drawing_memory &_dm,
                             int _num_threads,
                             unsigned int _max_iterations,
                             int _num_frames)
        : f0(_dm),
          f1(_dm),
          num_threads(_num_threads),
          num_frames(_num_frames)
{
    // set rendering areas
    f0.size_x = f1.size_x = _dm.sizex / 2 - 4;
    f0.size_y = f1.size_y = _dm.sizey - 4;
    f0.off_x = f0.off_y = f1.off_y = 2;
    f1.off_x = f0.size_x + 4 + 2;

    // set fractals parameters
    f0.cx = -0.6f;
    f0.cy = 0.0f;
    f0.magn = 200.0f;
    f1.cx = -0.6f;
    f1.cy = 0.0f;
    f1.magn = 200.0f;
    f0.max_iterations = f1.max_iterations = _max_iterations;

    // initially the first fractal is active
    active = 0;
    if (runtime == "tbb") {
        f_run[0] = std::unique_ptr<fractal_runtime>(new fractal_runtime_tbb(f0, num_frames, "tbb first"));
        f_run[1] = std::unique_ptr<fractal_runtime>(new fractal_runtime_tbb(f1, num_frames, "tbb second"));
    }
    else if (runtime == "omp") {
        f_run[0] = std::unique_ptr<fractal_runtime>(new fractal_runtime_omp(f0, num_frames, "omp first"));
        f_run[1] = std::unique_ptr<fractal_runtime>(new fractal_runtime_omp(f1, num_frames, "omp second"));
    }
    else if (runtime == "mix") {
        f_run[0] = std::unique_ptr<fractal_runtime>(new fractal_runtime_tbb(f0, num_frames, "tbb"));
        f_run[1] = std::unique_ptr<fractal_runtime>(new fractal_runtime_omp(f1, num_frames, "openmp"));
    }
    else {
        printf("Unknown runtime: %s\n", runtime.c_str());
        exit(1);
    }
}

void fractal_group::mouse_click(int x, int y) {
    // assumption that the point is not inside any fractal area
    int new_active = -1;

    if (f0.check_point(x, y)) {
        // the point is inside the first fractal area
        new_active = 0;
    }
    else if (f1.check_point(x, y)) {
        // the point is inside the second fractal area
        new_active = 1;
    }

    if (new_active != -1 && new_active != active) {
        switch_active(new_active);
    }
}
