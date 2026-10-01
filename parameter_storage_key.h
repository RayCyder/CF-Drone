#pragma once

#include <string.h>

// Preferences/NVS keys are limited to 15 characters. Keep the public
// parameter names unchanged while storing the longer names under short keys.
static inline const char *parameterStorageKey(const char *name) {
    if (strcmp(name, "EST_LVL_GATE_THR") == 0) return "EST_LVL_GATE";
    if (strcmp(name, "EST_LVL_BIAS_GAIN") == 0) return "EST_LVL_BIAS";
    if (strcmp(name, "SF_DESCEND_THRUST") == 0) return "SF_DESC_THRUST";
    return name;
}
