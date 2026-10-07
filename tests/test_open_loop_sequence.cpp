#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../open_loop_sequence.h"

static void parser_tests() {
    OpenLoopPackedStep steps[OPEN_LOOP_MAX_STEPS];
    const char *text = "0.1 0 0 0 0\n1.25,55.5,10,-20,30\n# comment\n";
    OpenLoopParseResult result = parseOpenLoopSequenceText(text, strlen(text), steps, OPEN_LOOP_MAX_STEPS);
    assert(result.ok);
    assert(result.count == 2);
    assert(result.totalMs == 1350);
    assert(result.schemaVersion == 1);
    assert(result.sourceKind == OPEN_LOOP_SOURCE_AUTHORED);
    assert(result.controlPolicy == OPEN_LOOP_POLICY_SLEW);
    assert(steps[0].durationMs == 100);
    assert(steps[1].durationMs == 1250);
    assert(steps[1].throttleCentipercent == 5550);
    assert(steps[1].rollCentipercent == 1000);
    assert(steps[1].pitchCentipercent == -2000);
    assert(steps[1].yawCentipercent == 3000);
    assert(!openLoopStepHasAltitude(steps[1]));
    assert(!openLoopStepHasHeading(steps[1]));

    const char *v2 = "# WEB_RC_RECORDED_V2\n0.5 48 0 0 0 -1.23 -91.5\n";
    result = parseOpenLoopSequenceText(v2, strlen(v2), steps, OPEN_LOOP_MAX_STEPS);
    assert(result.ok);
    assert(result.schemaVersion == 2);
    assert(result.sourceKind == OPEN_LOOP_SOURCE_RECORDED);
    assert(result.controlPolicy == OPEN_LOOP_POLICY_DIRECT);
    assert(result.count == 1);
    assert(steps[0].altitudeCentimeters == -123);
    assert(steps[0].headingDecidegrees == -915);
    assert(openLoopStepHasAltitude(steps[0]));
    assert(openLoopStepHasHeading(steps[0]));
    assert(fabsf(openLoopStepAltitudeMeters(steps[0]) + 1.23f) < 1e-6f);
    assert(fabsf(openLoopStepHeadingDegrees(steps[0]) + 91.5f) < 1e-6f);

    const char *authoredV2 = "# CF_ROUTE_META schema=2 source=authored policy=slew\n"
        "0.5 48 0 0 0 1.25 90\n";
    result = parseOpenLoopSequenceText(authoredV2, strlen(authoredV2), steps, OPEN_LOOP_MAX_STEPS);
    assert(result.ok && result.schemaVersion == 2);
    assert(result.sourceKind == OPEN_LOOP_SOURCE_AUTHORED);
    assert(result.controlPolicy == OPEN_LOOP_POLICY_SLEW);

    const char *recordedV1 = "# WEB_RC_RECORDED_V1\n0.5 48 0 0 0\n";
    result = parseOpenLoopSequenceText(recordedV1, strlen(recordedV1), steps, OPEN_LOOP_MAX_STEPS);
    assert(result.ok && result.schemaVersion == 1);
    assert(result.sourceKind == OPEN_LOOP_SOURCE_RECORDED);
    assert(result.controlPolicy == OPEN_LOOP_POLICY_DIRECT);

    const char *headerlessV2 = "0.5 48 0 0 0 1.0 0\n";
    result = parseOpenLoopSequenceText(headerlessV2, strlen(headerlessV2), steps, OPEN_LOOP_MAX_STEPS);
    assert(!result.ok && strcmp(result.reason, "schema_mismatch") == 0);

    const char *mixedV2 = "# WEB_RC_RECORDED_V2\n0.5 48 0 0 0 1.0 0\n0.5 48 0 0 0\n";
    result = parseOpenLoopSequenceText(mixedV2, strlen(mixedV2), steps, OPEN_LOOP_MAX_STEPS);
    assert(!result.ok && strcmp(result.reason, "schema_mismatch") == 0);

    const char *unknownMetadata = "# CF_ROUTE_META schema=2 source=recorded policy=slew\n"
        "0.5 48 0 0 0 1.0 0\n";
    result = parseOpenLoopSequenceText(unknownMetadata, strlen(unknownMetadata), steps, OPEN_LOOP_MAX_STEPS);
    assert(!result.ok && strcmp(result.reason, "schema_mismatch") == 0);

    const char *duplicateMetadata = "# WEB_RC_RECORDED_V2\n# WEB_RC_RECORDED_V2\n"
        "0.5 48 0 0 0 1.0 0\n";
    result = parseOpenLoopSequenceText(duplicateMetadata, strlen(duplicateMetadata), steps, OPEN_LOOP_MAX_STEPS);
    assert(!result.ok && strcmp(result.reason, "schema_mismatch") == 0);

    const char *lateMetadata = "# comment\n# CF_ROUTE_META schema=2 source=authored policy=slew\n"
        "0.5 48 0 0 0 1.0 0\n";
    result = parseOpenLoopSequenceText(lateMetadata, strlen(lateMetadata), steps, OPEN_LOOP_MAX_STEPS);
    assert(!result.ok && strcmp(result.reason, "schema_mismatch") == 0);

    const char *badV2 = "0.5 48 0 0 0 21 0\n";
    result = parseOpenLoopSequenceText(badV2, strlen(badV2), steps, OPEN_LOOP_MAX_STEPS);
    assert(!result.ok && strcmp(result.reason, "invalid_row") == 0);

    OpenLoopPackedStep retained = steps[0];
    const char *bad = "0.1 20 0 0 0\nnan 0 0 0 0\n";
    OpenLoopPackedStep staging[OPEN_LOOP_MAX_STEPS];
    result = parseOpenLoopSequenceText(bad, strlen(bad), staging, OPEN_LOOP_MAX_STEPS);
    assert(!result.ok && strcmp(result.reason, "invalid_row") == 0);
    assert(memcmp(&retained, &steps[0], sizeof(retained)) == 0);

    char full[OPEN_LOOP_MAX_STEPS * 12];
    full[0] = '\0';
    for (int i = 0; i < OPEN_LOOP_MAX_STEPS; ++i) strcat(full, "0.1 1 0 0 0\n");
    result = parseOpenLoopSequenceText(full, strlen(full), steps, OPEN_LOOP_MAX_STEPS);
    assert(result.ok && result.count == OPEN_LOOP_MAX_STEPS);
    strcat(full, "0.1 1 0 0 0\n");
    result = parseOpenLoopSequenceText(full, strlen(full), steps, OPEN_LOOP_MAX_STEPS);
    assert(!result.ok && strcmp(result.reason, "too_many_steps") == 0);

    char tooLarge[OPEN_LOOP_MAX_BODY + 2];
    memset(tooLarge, '1', sizeof(tooLarge));
    tooLarge[sizeof(tooLarge) - 1] = '\0';
    result = parseOpenLoopSequenceText(tooLarge, OPEN_LOOP_MAX_BODY + 1, steps, OPEN_LOOP_MAX_STEPS);
    assert(!result.ok && strcmp(result.reason, "body_too_large") == 0);
}

static void mapping_and_slew_tests() {
    OpenLoopPackedStep step{1000, 5000, 10000, -10000, 5000,
        OPEN_LOOP_ALTITUDE_SENTINEL_CM, OPEN_LOOP_HEADING_SENTINEL_DDEG};
    OpenLoopControls target{};
    openLoopMapStepToControls(step, 0.06f, 0.06f, 0.85f, 0.68f, 1.0f, target);
    assert(fabsf(target.roll - 0.85f) < 1e-6f);
    assert(fabsf(target.pitch + 0.85f) < 1e-6f);
    assert(target.yaw > 0.31f && target.yaw < 0.33f);
    assert(fabsf(target.throttle - 0.5f) < 1e-6f);
    assert(!target.hasAltitude);
    assert(!target.hasHeading);

    step.altitudeCentimeters = 200;
    step.headingDecidegrees = -1234;
    openLoopMapStepToControls(step, 0.06f, 0.06f, 0.85f, 0.68f, 1.0f, target);
    assert(target.hasAltitude && fabsf(target.altitudeMeters - 2.0f) < 1e-6f);
    assert(target.hasHeading && fabsf(target.headingDegrees + 123.4f) < 1e-5f);

    OpenLoopControls current{0, 0, 0, 0, false, NAN, false, NAN};
    openLoopSlewControls(current, target, 100);
    assert(fabsf(current.roll - 0.1f) < 1e-6f);
    assert(fabsf(current.pitch + 0.1f) < 1e-6f);
    assert(fabsf(current.throttle - 0.02f) < 1e-6f);
    openLoopSlewControls(current, target, 10000);
    assert(fabsf(current.roll - target.roll) < 1e-6f);
    assert(fabsf(current.throttle - target.throttle) < 1e-6f);
}

static void wrap_and_deadline_tests() {
    assert(!openLoopElapsed(UINT32_MAX - 4, UINT32_MAX - 2));
    assert(openLoopElapsed(1, UINT32_MAX - 2));
    assert(openLoopElapsed(1000, 1000));
}

static void start_offset_tests() {
    float altitudeOffset = 4.2f, headingOffset = -1.3f;
    openLoopResetStartOffsets(altitudeOffset, headingOffset);
    assert(altitudeOffset == 0.0f && headingOffset == 0.0f);
    altitudeOffset = -2.0f; headingOffset = 2.5f;
    openLoopResetStartOffsets(altitudeOffset, headingOffset);
    assert(altitudeOffset == 0.0f && headingOffset == 0.0f);
}

int main() {
    static_assert(sizeof(OpenLoopPackedStep) == 16, "packed route step size");
    parser_tests();
    mapping_and_slew_tests();
    wrap_and_deadline_tests();
    start_offset_tests();
    puts("open-loop parser/slew regression: PASS");
}
