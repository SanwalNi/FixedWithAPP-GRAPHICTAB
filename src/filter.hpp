#pragma once
#include <cmath>
#include <chrono>

/**
 * 1€ Filter (One Euro Filter)
 * Designed by Géry Casiez, Nicolas Roussel, Daniel Vogel (ACM CHI 2012).
 * Ideal for real-time graphics tablet and pointer input:
 * - At low speeds: strong filtering to eliminate hand tremor / jitter.
 * - At high speeds: zero filtering to maintain absolute zero latency and lag-free response.
 */
class LowPassFilter {
private:
    bool initialized = false;
    double prev_value = 0.0;

public:
    double filter(double value, double alpha) {
        if (!initialized) {
            initialized = true;
            prev_value = value;
            return value;
        }
        double result = alpha * value + (1.0 - alpha) * prev_value;
        prev_value = result;
        return result;
    }

    void reset() {
        initialized = false;
        prev_value = 0.0;
    }

    double get_last() const { return prev_value; }
};

class OneEuroFilter {
private:
    double min_cutoff; // Minimum cutoff frequency (Hz)
    double beta;       // Speed coefficient
    double d_cutoff;   // Cutoff frequency for derivative (Hz)
    LowPassFilter x_filter;
    LowPassFilter dx_filter;
    double last_time = 0.0;
    bool initialized = false;

    static double compute_alpha(double rate, double cutoff) {
        if (rate <= 0.0 || cutoff <= 0.0) return 1.0;
        double tau = 1.0 / (2.0 * 3.14159265358979323846 * cutoff);
        double te = 1.0 / rate;
        return 1.0 / (1.0 + tau / te);
    }

public:
    OneEuroFilter(double min_cutoff_hz = 1.2, double speed_coefficient = 0.005, double derivative_cutoff = 1.0)
        : min_cutoff(min_cutoff_hz), beta(speed_coefficient), d_cutoff(derivative_cutoff) {}

    void set_params(double min_hz, double speed_coeff) {
        min_cutoff = min_hz;
        beta = speed_coeff;
    }

    double filter(double value, double timestamp_sec) {
        if (!initialized) {
            initialized = true;
            last_time = timestamp_sec;
            dx_filter.reset();
            return x_filter.filter(value, 1.0);
        }

        double dt = timestamp_sec - last_time;
        last_time = timestamp_sec;

        if (dt <= 0.0001) {
            dt = 0.001; // Avoid divide by zero, assume minimum 1ms
        }

        double rate = 1.0 / dt;

        // Estimate derivative (velocity)
        double prev_x = x_filter.get_last();
        double dx = (value - prev_x) * rate;
        double dx_alpha = compute_alpha(rate, d_cutoff);
        double filtered_dx = dx_filter.filter(dx, dx_alpha);

        // Adaptive cutoff frequency based on velocity
        double cutoff = min_cutoff + beta * std::abs(filtered_dx);
        double alpha = compute_alpha(rate, cutoff);

        return x_filter.filter(value, alpha);
    }

    void reset() {
        initialized = false;
        last_time = 0.0;
        x_filter.reset();
        dx_filter.reset();
    }
};

class PointFilter2D {
private:
    OneEuroFilter filter_x;
    OneEuroFilter filter_y;

public:
    PointFilter2D(double min_cutoff = 1.0, double beta = 0.007)
        : filter_x(min_cutoff, beta), filter_y(min_cutoff, beta) {}

    void set_params(double min_cutoff, double beta) {
        filter_x.set_params(min_cutoff, beta);
        filter_y.set_params(min_cutoff, beta);
    }

    void filter_point(double in_x, double in_y, double timestamp_sec, double& out_x, double& out_y) {
        out_x = filter_x.filter(in_x, timestamp_sec);
        out_y = filter_y.filter(in_y, timestamp_sec);
    }

    void reset() {
        filter_x.reset();
        filter_y.reset();
    }
};
