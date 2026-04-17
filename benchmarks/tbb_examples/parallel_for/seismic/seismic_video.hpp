/*
   Copyright (C) 2005 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

*/

#ifndef TBB_examples_seismic_video_H
#define TBB_examples_seismic_video_H

#include "common/gui/video.hpp"

class Universe;

class SeismicVideo : public video {
#if defined(_WINDOWS) && !defined(_CONSOLE)
#define MAX_LOADSTRING 100
    TCHAR szWindowClass[MAX_LOADSTRING]; // the main window class name
    WNDCLASSEX wcex;
#endif
    static const char *const titles[2];

    bool initIsParallel;

    Universe &u_;
    int numberOfFrames_; // 0 means forever, positive means number of frames, negative is undefined
    int threadsHigh;

private:
    void on_mouse(int x, int y, int key);
    void on_process();

#if defined(_WINDOWS) && !defined(_CONSOLE)
public:
#endif
    void on_key(int key);

public:
    SeismicVideo(Universe &u, int numberOfFrames, int threadsHigh, bool initIsParallel = true);
};
#endif /* TBB_examples_seismic_video_H */
