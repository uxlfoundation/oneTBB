/*
    Copyright (C) 2005-2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef TBB_examples_seismic_universe_H
#define TBB_examples_seismic_universe_H

#ifndef UNIVERSE_WIDTH
#define UNIVERSE_WIDTH 1024
#endif
#ifndef UNIVERSE_HEIGHT
#define UNIVERSE_HEIGHT 512
#endif

#include "oneapi/tbb/partitioner.h"

#include "common/gui/video.hpp"

class Universe {
public:
    enum { UniverseWidth = UNIVERSE_WIDTH, UniverseHeight = UNIVERSE_HEIGHT };

private:
    //in order to avoid performance degradation due to cache aliasing issue
    //some padding is needed after each row in array, and between array themselves.
    //the padding is achieved by adjusting number of rows and columns.
    //as the compiler is forced to place class members of the same clause in order of the
    //declaration this seems to be the right way of padding.

    //magic constants added below are chosen experimentally for 1024x512.
    enum { MaxWidth = UniverseWidth + 1, MaxHeight = UniverseHeight + 3 };

    typedef float ValueType;

    //! Horizontal stress
    ValueType S[MaxHeight][MaxWidth];

    //! Velocity at each grid point
    ValueType V[MaxHeight][MaxWidth];

    //! Vertical stress
    ValueType T[MaxHeight][MaxWidth];

    //! Coefficient related to modulus
    ValueType M[MaxHeight][MaxWidth];

    //! Damping coefficients
    ValueType D[MaxHeight][MaxWidth];

    //! Coefficient related to lightness
    ValueType L[MaxHeight][MaxWidth];

    enum { ColorMapSize = 1024 };
    color_t ColorMap[4][ColorMapSize];

    enum MaterialType { WATER = 0, SANDSTONE = 1, SHALE = 2 };

    //! Values are MaterialType, cast to an unsigned char to save space.
    unsigned char material[MaxHeight][MaxWidth];

private:
    enum { DamperSize = 32 };

    std::string stress_runtime;
    std::string velocity_runtime;

    int pulseTime;
    int pulseCounter;
    int pulseX;
    int pulseY;

    drawing_memory drawingMemory;

public:
    void InitializeUniverse(video const& colorizer);

    void SerialUpdateUniverse();
    void ParallelUpdateUniverse();
    bool TryPutNewPulseSource(int x, int y);
    void SetDrawingMemory(const drawing_memory& dmem);

    void SetRuntimeForStress(std::string runtime);
    void SetRuntimeForVelocity(std::string runtime);

private:
    struct Rectangle;
    void UpdatePulse();
    void UpdateStress(Rectangle const& r);

    void SerialUpdateStress();
    friend struct UpdateStressBody;
    friend struct UpdateVelocityBody;
    void ParallelUpdateStress(oneapi::tbb::affinity_partitioner& affinity);

    void UpdateVelocity(Rectangle const& r);

    void SerialUpdateVelocity();
    void ParallelUpdateVelocity(oneapi::tbb::affinity_partitioner& affinity);
};

#endif /* TBB_examples_seismic_universe_H */
