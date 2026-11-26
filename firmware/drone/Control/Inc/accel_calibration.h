#ifndef ACCEL_CALIBRATION_H
#define ACCEL_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#define ACCEL_CAL_FACE_WINDOW_US 1000000U

/** Faces named by the sign of the specific force in body FRD. */
typedef enum {
    ACCEL_CAL_X_POS,
    ACCEL_CAL_X_NEG,
    ACCEL_CAL_Y_POS,
    ACCEL_CAL_Y_NEG,
    ACCEL_CAL_Z_POS,
    ACCEL_CAL_Z_NEG,
    ACCEL_CAL_NO_FACE,
} accel_cal_face_t;

#define ACCEL_CAL_ALL_FACES ((1U << ACCEL_CAL_NO_FACE) - 1U)

typedef struct {
    unsigned captured_faces;
    float face_mean_mps2[ACCEL_CAL_NO_FACE][3];
    struct {
        accel_cal_face_t face;
        uint32_t samples;
        uint32_t first_us;
        uint32_t last_us;
        float first_mps2[3];
        float mean_mps2[3];
        float m2[3];
    } window;
} accel_calibration_t;

/** Feed a new paired sample to a zero-initialized or active calibration.
 * Accel is uncorrected m/s^2 and gyro is bias-corrected rad/s, both in FRD.
 * t_read_us is the wrapping MCU read timestamp. min_samples is the minimum
 * count over ACCEL_CAL_FACE_WINDOW_US. Returns a face only when newly captured.
 * Motion and sample gaps reset the current window while preserving faces.
 * Pass a valid state and readable 3-element input arrays.
 */
accel_cal_face_t accel_calibration_push(accel_calibration_t *state,
                                        const float accel_mps2[3],
                                        const float gyro_radps[3],
                                        uint32_t t_read_us,
                                        uint32_t min_samples);

/** Fit bias (m/s^2, FRD) and a dimensionless correction matrix from six faces
 * aligned with the body axes. Apply matrix * (measured - bias), with rows
 * ordered by output axis and columns by input axis, both in FRD.
 * Pass a valid state and non-overlapping writable outputs. Returns false for
 * incomplete or inconsistent face data or out-of-range coefficients. Discard
 * the outputs on failure.
 */
bool accel_calibration_result(const accel_calibration_t *state,
                              float bias_mps2[3], float matrix[3][3]);

/** Check that the bias and all matrix coefficients are finite and within
 * calibration limits. Pass readable arrays with the layout described above.
 */
bool accel_calibration_valid(const float bias_mps2[3],
                             const float matrix[3][3]);

#endif
