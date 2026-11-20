#include "notch_filter.h"
#include <math.h>

notch_filter_coefficients_t
notch_filter_design(float sample_hz, float center_hz, float quality) {
    const float pi = 3.14159265358979323846f;
    const float center = center_hz / sample_hz;
    const float gain = 1.0f / (1.0f + tanf(pi * center / quality));
    return (notch_filter_coefficients_t){
        .b0 = gain,
        .b1 = -2.0f * gain * cosf(2.0f * pi * center),
        .a2 = 2.0f * gain - 1.0f,
    };
}

float notch_filter_step(const notch_filter_coefficients_t *coefficients,
                        notch_filter_state_t *state, float sample) {
    if (!isfinite(sample)) {
        *state = (notch_filter_state_t){0};
        return sample;
    }
    if (!state->initialized) {
        const float steady = (1.0f - coefficients->b0) * sample;
        *state = (notch_filter_state_t){
            .z1 = steady, .z2 = steady, .initialized = true};
        return sample;
    }

    /* Transposed direct form II with b2 = b0 and a1 = b1. */
    const float scaled = coefficients->b0 * sample;
    const float output = scaled + state->z1;
    state->z1 = coefficients->b1 * (sample - output) + state->z2;
    state->z2 = scaled - coefficients->a2 * output;
    return output;
}
