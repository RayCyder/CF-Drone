#pragma once

#include <stdint.h>

enum LevelCalibrationState : uint8_t {
    LEVEL_EMPTY, LEVEL_QUEUED, LEVEL_COLLECTING, LEVEL_PROCESSING,
    LEVEL_READY, LEVEL_APPLYING, LEVEL_APPLIED, LEVEL_REJECTED, LEVEL_CANCELLING
};

inline bool levelCalibrationBlocksArming(LevelCalibrationState state) {
    return state == LEVEL_QUEUED || state == LEVEL_COLLECTING ||
        state == LEVEL_PROCESSING || state == LEVEL_READY ||
        state == LEVEL_APPLYING || state == LEVEL_APPLIED ||
        state == LEVEL_CANCELLING;
}
