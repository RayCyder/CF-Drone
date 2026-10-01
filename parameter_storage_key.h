#pragma once

#include <string.h>
#include <strings.h>

// Preferences/NVS keys are limited to 15 characters. Keep the public
// parameter names unchanged while storing the longer names under short keys.
static inline const char *parameterStorageKey(const char *name) {
    if (strcmp(name, "EST_LVL_GATE_THR") == 0) return "EST_LVL_GATE";
    if (strcmp(name, "EST_LVL_BIAS_GAIN") == 0) return "EST_LVL_BIAS";
    if (strcmp(name, "SF_DESCEND_THRUST") == 0) return "SF_DESC_THRUST";
    return name;
}

// MAVLink PARAM_ID has exactly 16 bytes; use the existing short NVS alias
// only where the public name cannot fit in that field.
static inline const char *parameterMavlinkKey(const char *name) {
    return strlen(name) > 16 ? parameterStorageKey(name) : name;
}

static inline bool parameterNameMatches(const char *publicName, const char *name) {
    return name && (strcasecmp(publicName, name) == 0 ||
        strcasecmp(parameterMavlinkKey(publicName), name) == 0);
}
