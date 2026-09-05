#pragma once
// ============================================================================
//  1 Euro Filter (Casiez, Roussel, Vogel - CHI 2012)
//  Adaptive low-pass filter: strong jitter removal at low speed, zero added
//  lag at high speed. Identical math to the v1 engine, cleaned up.
// ============================================================================
#include <cmath>

class LowPassFilter {
public:
    double filter(double value, double alpha) {
        if (!initialized) {
            initialized = true;
            prev = value;
            return value;
        }
        prev = alpha * value + (1.0 - alpha) * prev;
        return prev;
    }
    void reset() { initialized = false; prev = 0.0; }
    double last() const { return prev; }

private:
    bool initialized = false;
    double prev = 0.0;
};

class OneEuroFilter {
public:
    OneEuroFilter(double min_cutoff_hz = 2.0, double beta = 0.010, double d_cutoff = 1.0)
        : min_cutoff(min_cutoff_hz), beta(beta), d_cutoff(d_cutoff) {}

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
        if (dt <= 0.0001) dt = 0.001;

        double rate = 1.0 / dt;
        double dx = (value - x_filter.last()) * rate;
        double dx_alpha = alpha(rate, d_cutoff);
        double filtered_dx = dx_filter.filter(dx, dx_alpha);

        double cutoff = min_cutoff + beta * std::abs(filtered_dx);
        return x_filter.filter(value, alpha(rate, cutoff));
    }

    void reset() {
        initialized = false;
        last_time = 0.0;
        x_filter.reset();
        dx_filter.reset();
    }

private:
    static double alpha(double rate, double cutoff) {
        if (rate <= 0.0 || cutoff <= 0.0) return 1.0;
        double tau = 1.0 / (6.28318530717958647692 * cutoff);
        double te = 1.0 / rate;
        return 1.0 / (1.0 + tau / te);
    }

    double min_cutoff;
    double beta;
    double d_cutoff;
    LowPassFilter x_filter;
    LowPassFilter dx_filter;
    double last_time = 0.0;
    bool initialized = false;
};

class PointFilter2D {
public:
    explicit PointFilter2D(double min_cutoff = 2.0, double beta = 0.010)
        : fx(min_cutoff, beta), fy(min_cutoff, beta) {}

    void set_params(double min_cutoff, double beta) {
        fx.set_params(min_cutoff, beta);
        fy.set_params(min_cutoff, beta);
    }

    void filter_point(double in_x, double in_y, double t, double& out_x, double& out_y) {
        out_x = fx.filter(in_x, t);
        out_y = fy.filter(in_y, t);
    }

    void reset() { fx.reset(); fy.reset(); }

private:
    OneEuroFilter fx;
    OneEuroFilter fy;
};
