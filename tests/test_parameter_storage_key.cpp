#include "../parameter_storage_key.h"

#include <cassert>
#include <cstring>

int main() {
    assert(std::strcmp(parameterStorageKey("EST_LVL_GATE_THR"), "EST_LVL_GATE") == 0);
    assert(std::strcmp(parameterStorageKey("EST_LVL_BIAS_GAIN"), "EST_LVL_BIAS") == 0);
    assert(std::strcmp(parameterStorageKey("SF_DESCEND_THRUST"), "SF_DESC_THRUST") == 0);
    assert(std::strcmp(parameterStorageKey("MOT_THR_MAX"), "MOT_THR_MAX") == 0);
    assert(std::strlen(parameterStorageKey("EST_LVL_GATE_THR")) <= 15);
    assert(std::strlen(parameterStorageKey("EST_LVL_BIAS_GAIN")) <= 15);
    assert(std::strlen(parameterStorageKey("SF_DESCEND_THRUST")) <= 15);
    assert(std::strcmp(parameterMavlinkKey("EST_LVL_GATE_THR"), "EST_LVL_GATE_THR") == 0);
    assert(std::strcmp(parameterMavlinkKey("EST_LVL_BIAS_GAIN"), "EST_LVL_BIAS") == 0);
    assert(std::strcmp(parameterMavlinkKey("SF_DESCEND_THRUST"), "SF_DESC_THRUST") == 0);
    assert(parameterNameMatches("EST_LVL_BIAS_GAIN", parameterMavlinkKey("EST_LVL_BIAS_GAIN")));
    assert(parameterNameMatches("SF_DESCEND_THRUST", parameterMavlinkKey("SF_DESCEND_THRUST")));
    assert(!parameterNameMatches("EST_LVL_BIAS_GAIN", "EST_LVL_BIAS_GAI"));
}
