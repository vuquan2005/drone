#ifndef NORMALIZED_H
#define NORMALIZED_H

#include <stdint.h>

/** Signed fixed-point with 15 fractional bits in a 32-bit container.
 * Real value = stored value / 32768. Both -1 and +1 are exact; intermediate
 * values may exceed that interval. API owners define the permitted range.
 * This representation does not imply thrust and ESC commands are equivalent.
 */
typedef int32_t norm_q15_t;

#define NORM_Q15_ONE INT32_C(32768)

#endif
