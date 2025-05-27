/*
    Copyright (C) 2005-2025 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef TBB_examples_fractal_video_H
#define TBB_examples_fractal_video_H

#include "common/gui/video.hpp"

#include "fractal.hpp"

extern video *v;
extern bool single;

class fractal_video : public video {
    fractal_group *fg;

private:
    void on_mouse(int x, int y, int key) {
        if (key == 1) {
            if (fg) {
                fg->set_num_frames_at_least(20);
                fg->mouse_click(x, y);
            }
        }
    }

    void on_key(int key) {
        switch (key & 0xff) {
            case esc_key: running = false; break;

            case 'q':
                if (fg)
                    fg->active_fractal_zoom_in();
                break;
            case 'e':
                if (fg)
                    fg->active_fractal_zoom_out();
                break;

            case 'r':
                if (fg)
                    fg->active_fractal_quality_inc();
                break;
            case 'f':
                if (fg)
                    fg->active_fractal_quality_dec();
                break;

            case 'w':
                if (fg)
                    fg->active_fractal_move_up();
                break;
            case 'a':
                if (fg)
                    fg->active_fractal_move_left();
                break;
            case 's':
                if (fg)
                    fg->active_fractal_move_down();
                break;
            case 'd':
                if (fg)
                    fg->active_fractal_move_right();
                break;
        }
        if (fg)
            fg->set_num_frames_at_least(20);
    }

    void on_process() {
        if (fg) {
            fg->run(!single);
        }
    }

public:
    fractal_video() : fg(nullptr) {
        title = "oneTBB: Fractal Example";
        v = this;
    }

    void set_fractal_group(fractal_group &_fg) {
        fg = &_fg;
    }
};

#endif /* TBB_examples_fractal_video_H */
