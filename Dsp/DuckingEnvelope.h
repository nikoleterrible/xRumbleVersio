#pragma once
#include <cmath>
#include <cstdint>

namespace rumble {

/**
 * DuckingEnvelope:
 * Generates an internal sidechain ducking curve whenever a kick is triggered.
 * Allows the punch of the kick to breathe cleanly while providing rhythmic pumping.
 */
class DuckingEnvelope {
public:
    DuckingEnvelope() : sample_rate_(48000.0f) {}

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        current_env_ = 1.0f; // 1.0 = full rumble volume, 0.0 = completely ducked
        hold_counter_ = 0;
        depth_ = 1.0f;
        release_coeff_ = 0.999f;
        curve_shape_ = 1.0f;
    }

    void Trigger() {
        // Fast attack drop to duck volume (over ~1 ms)
        current_env_ = 0.0f;
        // Hold ducking for 42 ms so kick punch and beater attack pass in total silence
        hold_counter_ = static_cast<uint32_t>(sample_rate_ * 0.042f);
    }

    /**
     * @param depth_knob: 0.0 (no ducking) to 1.0 (full duck down to silence)
     * @param speed_knob: 0.0 (snappy 1/16th release) to 1.0 (slow 1/4 note release)
     * @param beat_interval_samples: tempo reference for synced mode (0 if free)
     */
    void SetParameters(float depth_knob, float speed_knob, uint32_t beat_interval_samples = 0) {
        depth_ = depth_knob;
        if (depth_ < 0.0f) depth_ = 0.0f;
        if (depth_ > 1.0f) depth_ = 1.0f;

        float release_time_sec;
        if (beat_interval_samples > 0) {
            // Synced mode: map speed_knob across musical divisions:
            // 0.0 -> 1/16th note, 0.5 -> 1/8th note, 1.0 -> 1/4 note
            float beat_sec = static_cast<float>(beat_interval_samples) / sample_rate_;
            if (speed_knob < 0.33f) {
                release_time_sec = beat_sec * 0.25f; // 1/16th note
            } else if (speed_knob < 0.66f) {
                release_time_sec = beat_sec * 0.50f; // 1/8th note
            } else {
                release_time_sec = beat_sec * 0.75f; // dotted 8th / full swell
            }
        } else {
            // Free mode: continuous 40ms to 550ms
            release_time_sec = 0.040f + (speed_knob * speed_knob) * 0.50f;
        }

        // Calculate single-pole lowpass release coefficient
        // y[n] = y[n-1] + (1 - coeff) * (target - y[n-1])
        float samples = release_time_sec * sample_rate_;
        if (samples < 1.0f) samples = 1.0f;
        release_rate_ = 1.0f - expf(-3.0f / samples);
    }

    // Process a single audio sample and return the multiplicative gain factor [0.0 to 1.0]
    float Process() {
        if (hold_counter_ > 0) {
            hold_counter_--;
            current_env_ = 0.0f;
        } else {
            // Asymptotic release towards 1.0
            current_env_ += release_rate_ * (1.0f - current_env_);
            if (current_env_ > 1.0f) current_env_ = 1.0f;
        }

        // Curve shaping: square curve for more dramatic "sucking" pump
        float shaped = current_env_ * current_env_;

        // Apply ducking depth:
        // When depth_ is 1.0: gain goes from 0.0 to 1.0
        // When depth_ is 0.0: gain is constant 1.0
        return (1.0f - depth_) + depth_ * shaped;
    }

    // Returns the current raw ducking state for LED visualization [0.0 = fully ducked, 1.0 = open]
    float GetEnvelopeValue() const {
        return current_env_;
    }

private:
    float sample_rate_;
    float current_env_;
    uint32_t hold_counter_;
    float depth_;
    float release_rate_;
    float release_coeff_;
    float curve_shape_;
};

} // namespace rumble
