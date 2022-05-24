/* This file is part of ESPress0x20 and distributed under the terms of the
 * MIT license. See COPYING.
 */

#include "PID_Tuner.h"
#include "Arduino.h"

PID_Tuner::PID_Tuner(float *in, float *out) {
    input = in;
    output = out;
    noise_band = 1.0;
    sample_time = 1000;
    reset();
}

void
PID_Tuner::reset(void) {
    ts_last = millis();
    finished = false;
    peak_type = 0;
    peak_count = 0;
    input_valid = false;
    input_max = *input;
    input_min = input_max;
    setpoint = 214.5;
    output_high = 500;
    output_low = 0;
    input_idx = 0;
    Ku = 1.0;
    Pu = 1.0;
}

bool
PID_Tuner::run(void) {
    if (finished) {
        return true;
    }

    unsigned long now = millis();

    if ((now - ts_last) < sample_time) {
        return false;
    }

    ts_last = now;
    float current_input = *input;
    if (current_input > input_max) {
        input_max = current_input;
    }
    if (current_input < input_min) {
        input_min = current_input;
    }

    // Steer output in the correct direction
    if (current_input > setpoint + noise_band) {
        *output = output_low;
    } else if (current_input < setpoint - noise_band) {
        *output = output_high;
    }

    // Check for peak
    bool is_max = true;
    bool is_min = true;
    for (int i = 0; i < 100; i++) {
        if (is_max) {
            is_max = current_input > input_ring[ i ];
        }
        if (is_min) {
            is_min = current_input < input_ring[ i ];
        }
    }
    input_ring[ input_idx++ ] = current_input;
    if (input_idx >= 100) {
        input_valid = true;
        input_idx = 0;
    }

    if (!input_valid) {
        return false;
    }

    if (is_max) {
        if (peak_type == -1) {
            ts_penultimate = ts_ultimate;
        }
        peak_type = 1;
        ts_ultimate = now;
        peaks[ peak_count ] = current_input;
    } else if (is_min) {
        if (peak_type == 1) {
            peak_count++;
        }
        peak_type = -1;

        if (peak_count < 10) {
            peaks[ peak_count ] = current_input;
        }
    }

    if (is_max || is_min) {
        Ku = 4 * (output_high - output_low) /
             ((input_max - input_min) * 3.14159);

        if (peak_count > 2) {
            Pu = (float)(ts_ultimate - ts_penultimate) / 1000;

            // Check if we're done
            float separation =
                    (abs(peaks[ peak_count - 1 ] - peaks[ peak_count - 2 ]) +
                            abs(peaks[ peak_count - 2 ] -
                                    peaks[ peak_count - 3 ])) /
                    2;
            if (separation < 0.05 * (input_max - input_min)) {
                finished = true;
            }
        }
    }

    if (peak_count >= 10) {
        finished = true;
    }

    return finished;
}

float
PID_Tuner::get_p() {
    return 0.6 * Ku;
}

float
PID_Tuner::get_i() {
    return 1.2 * Ku / Pu;
}

float
PID_Tuner::get_d() {
    return 0.075 * Ku * Pu;
}
