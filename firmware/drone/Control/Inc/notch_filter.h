#ifndef NOTCH_FILTER_H
#define NOTCH_FILTER_H

#include <stdbool.h>

/** Coefficients shared by all channels of a second-order IIR notch. */
typedef struct {
    float b0;
    float b1;
    float a2;
} notch_filter_coefficients_t;

/** Per-channel history. Zero-initialize before use or after a sample gap. */
typedef struct {
    float z1;
    float z2;
    bool initialized;
} notch_filter_state_t;

/**
 * @brief Design a notch with unity gain at DC.
 * @param sample_hz Uniform input sample rate, in Hz.
 * @param center_hz Notch center in Hz, strictly between DC and Nyquist.
 * @param quality Center frequency divided by the -3 dB bandwidth. The
 * resulting bandwidth must be positive and below Nyquist.
 * @return Coefficients for notch_filter_step().
 * @pre All parameters are finite and meet the frequency constraints above.
 */
notch_filter_coefficients_t notch_filter_design(float sample_hz,
                                                float center_hz, float quality);

/**
 * @brief Advance one channel by one sample at the design sample rate.
 * @param coefficients Valid design coefficients.
 * @param state Writable channel history, zero-initialized or already active.
 * @param sample Input in the caller's units.
 * @return Filtered value in the same units. The first sample is passed through
 * and primes the history at steady state. A non-finite sample is returned
 * unchanged and clears the history.
 */
float notch_filter_step(const notch_filter_coefficients_t *coefficients,
                        notch_filter_state_t *state, float sample);

#endif
