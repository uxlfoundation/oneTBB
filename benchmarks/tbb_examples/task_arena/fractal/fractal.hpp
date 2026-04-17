/*
   Copyright (C) 2005 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

*/

#ifndef TBB_examples_fractal_H
#define TBB_examples_fractal_H

#include <atomic>
#include <future>
#include <memory>

#include "oneapi/tbb/task_arena.h"
#include "oneapi/tbb/task_group.h"

#include "common/gui/video.hpp"
#include "common/utility/get_default_num_threads.hpp"

//! Fractal class
class fractal {
    //! Left corner of the fractal area
    int off_x, off_y;
    //! Size of the fractal area
    int size_x, size_y;

    //! Fractal properties
    float cx, cy;
    float magn;
    float step;
    unsigned int max_iterations;

    //! Drawing memory object for rendering
    const drawing_memory &dm;

    //! One pixel calculation routine
    color_t calc_one_pixel(int x, int y) const;
    //! Draws the border around the fractal area
    void draw_border(bool is_active);
    //! Check if the point is inside the fractal area
    bool check_point(int x, int y) const;

public:
    //! Constructor
    fractal(const drawing_memory &dm) : step(0.2), dm(dm) {
#if _MSC_VER && _WIN64 && !__INTEL_COMPILER
        // Workaround for MSVC x64 compiler issue
        volatile int i = 0;
#endif
    }
    //! Renders the fractal rectangular area
    void render_rect(int x0, int y0, int x1, int y1) const;

    //! Clears the fractal area
    void clear();

    void move_up() {
        cy += step;
    }
    void move_down() {
        cy -= step;
    }
    void move_left() {
        cx += step;
    }
    void move_right() {
        cx -= step;
    }

    void zoom_in() {
        magn *= 2.;
        step /= 2.;
    }
    void zoom_out() {
        magn /= 2.;
        step *= 2.;
    }

    void quality_inc() {
        max_iterations += max_iterations / 2;
    }
    void quality_dec() {
        max_iterations -= max_iterations / 2;
    }

    int get_size_x() const {
        return size_x;
    }

    int get_size_y() const {
        return size_y;
    }

    friend class fractal_group;
};

class fractal_runtime {
public:
    fractal_runtime(fractal &_f, int& _num_frames, const char* _name)
        : num_frames(_num_frames), f(_f), name(_name) {}
    virtual ~fractal_runtime() = default;
    void calc_fractal();
    virtual void render() {};
    virtual void run() {}
    virtual void wait() {}
    virtual void run_and_wait() {}
    virtual void cancel() {}
    virtual void initialize(int /*num_threads*/) {}
protected:
    //! Number of frames to calculate
    int& num_frames;
    // fractal to execute
    fractal& f;
    // name of the runtime
    const char* name;
};

class fractal_runtime_tbb : public fractal_runtime {
public:
    fractal_runtime_tbb(fractal &_f, int& _num_frames, const char* name = "tbb")
        : fractal_runtime(_f, _num_frames, name) {}

    // Exception specification is needed here since base class's destructor is not
    // potentially-throwing, but the override cannot have laxer specification. See [except.spec].
    ~fractal_runtime_tbb() noexcept override = default;

    void initialize(int num_threads) override {
        ta.initialize(num_threads);
    }

    void cancel() override {
        context.cancel_group_execution();
    }

    void run() override {
        ta.execute([&] {
            tg.run([&] {
                calc_fractal();
            });
        });
    }

    void wait() override {
        ta.execute([&] {
            tg.wait();
        });
    }

    void run_and_wait() override {
        ta.execute([&] {
            calc_fractal();
        });
    }

    void render() override;
private:
    oneapi::tbb::task_group_context context{};
    oneapi::tbb::task_arena ta{};
    oneapi::tbb::task_group tg{};
};

class fractal_runtime_omp : public fractal_runtime {
public:
    fractal_runtime_omp(fractal &_f, int& _num_frames, const char* name = "tbb")
        : fractal_runtime(_f, _num_frames, name) {}

    void initialize(int _num_threads) override {
        nthreads = _num_threads;
    }

    void run() override {
        async_context = std::async(std::launch::async, [&] {
            calc_fractal();
        });
    }

    void wait() override {
        async_context.wait();
    }

    void run_and_wait() override {
        calc_fractal();
    }

    void render() override;
private:
    int nthreads;
    std::future<void> async_context;
};

//! The group of fractals
class fractal_group {
    //! Fractals definition
    fractal f0, f1;

    //! The number of threads
    int num_threads;

    //! The number of frames
    int num_frames;

    std::unique_ptr<fractal_runtime> f_run[2];
    //! Border type enumeration
    enum BORDER_TYPE { BORDER_INACTIVE = 0, BORDER_ACTIVE };

    //! The active (high priority) fractal number
    int active;

    //! Draws the borders around the fractals
    void draw_borders();
    //! Sets priorities for fractals calculations
    void set_priorities();

public:
    //! Constructor
    fractal_group(const drawing_memory &_dm,
                  int num_threads = utility::get_default_num_threads(),
                  unsigned int max_iterations = 100000,
                  int num_frames = 1);
    //! Run calculation
    void run(int num_threads, bool create_second_fractal);
    void run(bool create_second_fractal = true) {
        run(num_threads, create_second_fractal);
    }
    //! Mouse event handler
    void mouse_click(int x, int y);
    //! Reset the number of frames to be not less than the given value
    void set_num_frames_at_least(int n);
    //! Switch active fractal
    void switch_active(int new_active = -1);
    //! Get active fractal
    fractal &get_active_fractal() {
        return active ? f1 : f0;
    }

    void active_fractal_zoom_in() {
        get_active_fractal().zoom_in();
        f_run[active]->cancel();
    }
    void active_fractal_zoom_out() {
        get_active_fractal().zoom_out();
        f_run[active]->cancel();
    }
    void active_fractal_quality_inc() {
        get_active_fractal().quality_inc();
        f_run[active]->cancel();
    }
    void active_fractal_quality_dec() {
        get_active_fractal().quality_dec();
        f_run[active]->cancel();
    }
    void active_fractal_move_up() {
        get_active_fractal().move_up();
        f_run[active]->cancel();
    }
    void active_fractal_move_down() {
        get_active_fractal().move_down();
        f_run[active]->cancel();
    }
    void active_fractal_move_left() {
        get_active_fractal().move_left();
        f_run[active]->cancel();
    }
    void active_fractal_move_right() {
        get_active_fractal().move_right();
        f_run[active]->cancel();
    }
};

#endif /* TBB_examples_fractal_H */
