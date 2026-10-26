#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace rumble {

/**
 * RhythmicDelay:
 * Delay buffer positioned in front of the reverb diffuser.
 * Provides pre-delay timing to separate kick attack from the sub swelling,
 * and allows feeding back repeats ('Texture') for classic rolling techno polyrhythms.
 */
class RhythmicDelay {
public:
    static constexpr size_t kMaxDelaySamples = 24000; // 500 ms at 48kHz

    RhythmicDelay() : sample_rate_(48000.0f), write_idx_(0), delay_samples_(1200.0f), feedback_(0.0f), lp_state_(0.0f) {}

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        write_idx_ = 0;
        delay_samples_ = sample_rate * 0.05f; // 50ms default pre-delay
        feedback_ = 0.0f;
        lp_state_ = 0.0f;
        std::fill(buffer_, buffer_ + kMaxDelaySamples, 0.0f);
    }

    /**
     * @param delay_knob: 0.0 (near-zero pre-delay ~5ms) to 1.0 (long pre-delay ~150ms)
     * @param texture_knob: 0.0 (single tap, no feedback) to 1.0 (dense rolling feedback)
     * @param beat_interval_samples: tempo reference if in sync mode (0 if free)
     * @param sync_triplets: if true in sync mode, use triplet divisions
     */
    void SetParameters(float delay_knob, float texture_knob, uint32_t beat_interval_samples = 0, bool sync_triplets = false) {
        // Texture / Feedback: 0.0 to 0.78 (bounded below oscillation runaway)
        feedback_ = texture_knob * 0.78f;

        if (beat_interval_samples > 0) {
            float beat_samples = static_cast<float>(beat_interval_samples);
            if (sync_triplets) {
                // Triplet division
                if (delay_knob < 0.5f) {
                    delay_samples_ = beat_samples * (1.0f / 6.0f); // 16th triplet
                } else {
                    delay_samples_ = beat_samples * (1.0f / 3.0f); // 8th triplet
                }
            } else {
                // Straight division
                if (delay_knob < 0.33f) {
                    delay_samples_ = beat_samples * 0.125f; // 32nd note
                } else if (delay_knob < 0.66f) {
                    delay_samples_ = beat_samples * 0.25f;  // 16th note
                } else {
                    delay_samples_ = beat_samples * 0.50f;  // 8th note
                }
            }
        } else {
            // Free mode: 5ms to 160ms
            delay_samples_ = 0.005f * sample_rate_ + (delay_knob * delay_knob) * (0.155f * sample_rate_);
        }

        if (delay_samples_ < 1.0f) delay_samples_ = 1.0f;
        if (delay_samples_ >= kMaxDelaySamples - 2) delay_samples_ = kMaxDelaySamples - 2;
    }

    // Process single audio sample
    float Process(float in) {
        // Read with linear interpolation
        float read_pos = static_cast<float>(write_idx_) - delay_samples_;
        if (read_pos < 0.0f) {
            read_pos += kMaxDelaySamples;
        }

        size_t idx0 = static_cast<size_t>(read_pos);
        size_t idx1 = (idx0 + 1) % kMaxDelaySamples;
        float frac = read_pos - static_cast<float>(idx0);

        float delayed = buffer_[idx0] + frac * (buffer_[idx1] - buffer_[idx0]);

        // Feedback path with 1-pole damping filter (~600 Hz) and gentle saturation
        lp_state_ += 0.25f * (delayed - lp_state_);
        float sat_fb = tanhf(lp_state_ * 1.2f) * 0.833f;

        // Write to circular buffer
        buffer_[write_idx_] = in + sat_fb * feedback_;
        write_idx_ = (write_idx_ + 1) % kMaxDelaySamples;

        return delayed;
    }

private:
    float sample_rate_;
    float buffer_[kMaxDelaySamples];
    size_t write_idx_;
    float delay_samples_;
    float feedback_;
    float lp_state_;
};

} // namespace rumble
