#pragma once
#include <cmath>
#include <cstdint>

namespace rumble {

/**
 * TransientDetector:
 * Robust onset rise detector tailored for kick punch transients.
 * Uses spectral flux / onset surge detection (fast envelope vs slow background energy)
 * to reliably trigger ducking even on kicks with very long sub tails without false double-triggers.
 */
class TransientDetector {
public:
    TransientDetector() : sample_rate_(48000.0f) {}

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        fast_env_ = 0.0f;
        slow_env_ = 0.0f;
        cooldown_samples_ = 0;
        sample_counter_ = 0;
        last_interval_samples_ = static_cast<uint32_t>(sample_rate_ * 0.4615f); // ~130 BPM
        smoothed_interval_ = static_cast<float>(last_interval_samples_);

        // 2nd order bandpass filter around 160 Hz (punch region)
        // Q ~ 1.5
        float omega = 2.0f * 3.1415926535f * 160.0f / sample_rate_;
        float alpha = sinf(omega) / (2.0f * 1.5f);
        b0_ = alpha;
        b1_ = 0.0f;
        b2_ = -alpha;
        a0_ = 1.0f + alpha;
        a1_ = -2.0f * cosf(omega);
        a2_ = 1.0f - alpha;

        w1_ = 0.0f;
        w2_ = 0.0f;
    }

    /**
     * Process an input sample
     * Returns true on the exact onset of a kick punch.
     */
    bool Process(float in, bool external_gate = false) {
        sample_counter_++;

        if (cooldown_samples_ > 0) {
            cooldown_samples_--;
        }

        // External gate takes precedence if active
        if (external_gate && cooldown_samples_ == 0) {
            RegisterHit();
            return true;
        }

        // Direct form II biquad bandpass filter
        float w0 = (in - a1_ / a0_ * w1_ - a2_ / a0_ * w2_);
        float bp = (b0_ / a0_ * w0 + b1_ / a0_ * w1_ + b2_ / a0_ * w2_);
        w2_ = w1_;
        w1_ = w0;

        float abs_val = fabsf(bp);

        // Fast energy follower (attack 2ms, decay 20ms)
        if (abs_val > fast_env_) {
            fast_env_ += 0.25f * (abs_val - fast_env_);
        } else {
            fast_env_ *= 0.998f;
        }

        // Slow background energy follower (~40ms time constant)
        slow_env_ += 0.001f * (fast_env_ - slow_env_);

        // Onset surge: fast energy exceeds slow background energy
        float onset_strength = fast_env_ - (slow_env_ * 1.5f);

        if (onset_strength > 0.06f && fast_env_ > 0.08f && cooldown_samples_ == 0) {
            RegisterHit();
            return true;
        }

        return false;
    }

    uint32_t GetBeatIntervalSamples() const {
        return static_cast<uint32_t>(smoothed_interval_);
    }

    float GetEstimatedBPM() const {
        if (smoothed_interval_ <= 0.0f) return 130.0f;
        return (60.0f * sample_rate_) / smoothed_interval_;
    }

private:
    void RegisterHit() {
        if (sample_counter_ > static_cast<uint32_t>(sample_rate_ * 0.18f) &&
            sample_counter_ < static_cast<uint32_t>(sample_rate_ * 1.5f)) {
            last_interval_samples_ = sample_counter_;
            smoothed_interval_ = 0.7f * smoothed_interval_ + 0.3f * static_cast<float>(sample_counter_);
        }
        sample_counter_ = 0;
        // Hold-off: 200 ms (accommodates up to 300 BPM cleanly)
        cooldown_samples_ = static_cast<uint32_t>(sample_rate_ * 0.200f);
    }

    float sample_rate_;
    float fast_env_;
    float slow_env_;
    uint32_t cooldown_samples_;
    uint32_t sample_counter_;
    uint32_t last_interval_samples_;
    float smoothed_interval_;

    float b0_, b1_, b2_, a0_, a1_, a2_;
    float w1_, w2_;
};

} // namespace rumble
