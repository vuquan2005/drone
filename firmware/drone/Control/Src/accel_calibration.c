#include "accel_calibration.h"
#include <math.h>
#include <string.h>

#define GRAVITY_MPS2 9.80665f
#define ACCEL_MIN_MPS2 (0.85f * GRAVITY_MPS2)
#define ACCEL_MAX_MPS2 (1.15f * GRAVITY_MPS2)
#define FACE_MIN_COS 0.98480775f /* 10 degrees from the body axis. */
#define MAX_GYRO_RADPS 0.025f
#define MAX_ACCEL_DELTA_MPS2 0.25f
#define MAX_ACCEL_STDDEV_MPS2 0.08f
#define MAX_PAIR_BIAS_ERROR_MPS2 0.25f
#define MAX_CROSS_AXIS_CORRECTION 0.20f
#define MAX_SAMPLE_GAP_US 20000U

static accel_cal_face_t identify_face(const float accel_mps2[3],
                                      const float gyro_radps[3]) {
    float norm_sq = 0.0f;
    float dominant_sq = 0.0f;
    unsigned dominant_axis = 0U;
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        if (!(fabsf(gyro_radps[axis]) <= MAX_GYRO_RADPS)) {
            return ACCEL_CAL_NO_FACE;
        }
        const float component_sq = accel_mps2[axis] * accel_mps2[axis];
        norm_sq += component_sq;
        if (component_sq > dominant_sq) {
            dominant_sq = component_sq;
            dominant_axis = axis;
        }
    }
    if (!(norm_sq >= ACCEL_MIN_MPS2 * ACCEL_MIN_MPS2 &&
          norm_sq <= ACCEL_MAX_MPS2 * ACCEL_MAX_MPS2 &&
          dominant_sq >= FACE_MIN_COS * FACE_MIN_COS * norm_sq)) {
        return ACCEL_CAL_NO_FACE;
    }
    return (accel_cal_face_t)(2U * dominant_axis +
                              (accel_mps2[dominant_axis] < 0.0f ? 1U : 0U));
}

accel_cal_face_t accel_calibration_push(accel_calibration_t *state,
                                        const float accel_mps2[3],
                                        const float gyro_radps[3],
                                        uint32_t t_read_us,
                                        uint32_t min_samples) {
    const accel_cal_face_t face = identify_face(accel_mps2, gyro_radps);
    if (face == ACCEL_CAL_NO_FACE || (state->captured_faces & (1U << face))) {
        state->window.samples = 0U;
        return ACCEL_CAL_NO_FACE;
    }
    if (state->window.samples == 0U || state->window.face != face ||
        t_read_us - state->window.last_us > MAX_SAMPLE_GAP_US) {
        memset(&state->window, 0, sizeof(state->window));
        state->window.face = face;
        state->window.first_us = t_read_us;
        memcpy(state->window.first_mps2, accel_mps2,
               sizeof(state->window.first_mps2));
    }
    state->window.last_us = t_read_us;
    ++state->window.samples;
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        if (fabsf(accel_mps2[axis] - state->window.first_mps2[axis]) >
            MAX_ACCEL_DELTA_MPS2) {
            state->window.samples = 0U;
            return ACCEL_CAL_NO_FACE;
        }
        const float delta = accel_mps2[axis] - state->window.mean_mps2[axis];
        state->window.mean_mps2[axis] += delta / (float)state->window.samples;
        state->window.m2[axis] +=
            delta * (accel_mps2[axis] - state->window.mean_mps2[axis]);
    }
    if (t_read_us - state->window.first_us < ACCEL_CAL_FACE_WINDOW_US) {
        return ACCEL_CAL_NO_FACE;
    }
    const uint32_t samples = state->window.samples;
    state->window.samples = 0U;
    if (samples < min_samples || samples < 2U) {
        return ACCEL_CAL_NO_FACE;
    }
    const float max_m2 =
        MAX_ACCEL_STDDEV_MPS2 * MAX_ACCEL_STDDEV_MPS2 * (float)(samples - 1U);
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        if (state->window.m2[axis] > max_m2) {
            return ACCEL_CAL_NO_FACE;
        }
    }
    memcpy(state->face_mean_mps2[face], state->window.mean_mps2,
           sizeof(state->face_mean_mps2[face]));
    state->captured_faces |= 1U << face;
    return face;
}

bool accel_calibration_valid(const float bias_mps2[3],
                             const float matrix[3][3]) {
    for (unsigned row = 0U; row < 3U; ++row) {
        if (!(fabsf(bias_mps2[row]) <= 0.15f * GRAVITY_MPS2 &&
              matrix[row][row] >= 0.85f && matrix[row][row] <= 1.15f)) {
            return false;
        }
        for (unsigned col = 0U; col < 3U; ++col) {
            if (row != col &&
                !(fabsf(matrix[row][col]) <= MAX_CROSS_AXIS_CORRECTION)) {
                return false;
            }
        }
    }
    return true;
}

static void cross_product(const float left[3], const float right[3],
                          float result[3]) {
    result[0] = left[1] * right[2] - left[2] * right[1];
    result[1] = left[2] * right[0] - left[0] * right[2];
    result[2] = left[0] * right[1] - left[1] * right[0];
}

bool accel_calibration_result(const accel_calibration_t *state,
                              float bias_mps2[3], float matrix[3][3]) {
    if (state->captured_faces != ACCEL_CAL_ALL_FACES) {
        return false;
    }
    float response_col[3][3];
    float pair_bias_mps2[3][3];
    for (unsigned face_axis = 0U; face_axis < 3U; ++face_axis) {
        const float *positive = state->face_mean_mps2[2U * face_axis];
        const float *negative = state->face_mean_mps2[2U * face_axis + 1U];
        if (!(positive[face_axis] > 0.0f && negative[face_axis] < 0.0f)) {
            return false;
        }
        for (unsigned axis = 0U; axis < 3U; ++axis) {
            response_col[face_axis][axis] =
                (positive[axis] - negative[axis]) / (2.0f * GRAVITY_MPS2);
            pair_bias_mps2[face_axis][axis] =
                0.5f * (positive[axis] + negative[axis]);
        }
    }
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        bias_mps2[axis] = (pair_bias_mps2[0][axis] + pair_bias_mps2[1][axis] +
                           pair_bias_mps2[2][axis]) /
                          3.0f;
        for (unsigned pair = 0U; pair < 3U; ++pair) {
            if (!(fabsf(pair_bias_mps2[pair][axis] - bias_mps2[axis]) <=
                  MAX_PAIR_BIAS_ERROR_MPS2)) {
                return false;
            }
        }
    }

    /* The inverse rows are cross products of the measured response columns,
     * divided by the determinant. This maps measured FRD back to ideal FRD. */
    cross_product(response_col[1], response_col[2], matrix[0]);
    cross_product(response_col[2], response_col[0], matrix[1]);
    cross_product(response_col[0], response_col[1], matrix[2]);
    const float determinant = response_col[0][0] * matrix[0][0] +
                              response_col[0][1] * matrix[0][1] +
                              response_col[0][2] * matrix[0][2];
    if (!(determinant > 0.0f)) {
        return false;
    }
    const float inverse_det = 1.0f / determinant;
    for (unsigned row = 0U; row < 3U; ++row) {
        for (unsigned col = 0U; col < 3U; ++col) {
            matrix[row][col] *= inverse_det;
        }
    }
    /* C11 requires an explicit qualifier conversion for array pointers. */
    return accel_calibration_valid(bias_mps2, (const float (*)[3])matrix);
}
