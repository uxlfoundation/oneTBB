#ifndef __TBB_examples_nbody_video_HPP
#define __TBB_examples_nbody_video_HPP

#include "common/gui/video.hpp"

class Universe;

class NBodyVideo : public video {
    Universe& m_left_universe;
    Universe& m_right_universe;
    std::size_t m_num_frames;
    char m_title_buf[128];

    void on_process() override;
public:
    NBodyVideo(Universe& left, Universe& right, std::size_t num_frames);
};

#endif // __TBB_examples_nbody_video_HPP
