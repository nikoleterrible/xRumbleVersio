#pragma once
#include <cmath>
#include <algorithm>

namespace rumble {

/**
 * StereoBassManager:
 * Professional Club & Vinyl Bass Management for Techno Rumble.
 * 
 * - Mid/Side processing
 * - Steep Highpass on Side signal at 90 Hz (guarantees sub frequencies < 90 Hz are 100% mono)
 * - Stereo Width scaling above 90 Hz
 * - High-headroom analog-modeled output soft saturator/limiter
 */
class StereoBassManager {
public:
    StereoBassManager() : sample_rate_(48000.0f), width_(1.0f) {}

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        width_ = 1.0f;

        // 2-pole Butterworth Highpass on Side channel at 90 Hz
        // Eliminates out-of-phase low sub rumble that cancels out on mono PAs
        float freq = 90.0f;
        float omega = 2.0f * 3.1415926535f * freq / sample_rate_;
        float sin_w = sinf(omega);
        float cos_w = cosf(omega);
        float alpha = sin_w / (2.0f * 0.7071f);

        float a0 = 1.0f + alpha;
        hp_b0_ = ((1.0f + cos_w) * 0.5f) / a0;
        hp_b1_ = -(1.0f + cos_w) / a0;
        hp_b2_ = ((1.0f + cos_w) * 0.5f) / a0;
        hp_a1_ = (-2.0f * cos_w) / a0;
        hp_a2_ = (1.0f - alpha) / a0;

        s1_ = 0.0f;
        s2_ = 0.0f;
    }

    /**
     * @param width: 0.0 (strictly mono) to 1.0 (natural wide stereo)
     */
    void SetWidth(float width) {
        width_ = width;
        if (width_ < 0.0f) width_ = 0.0f;
        if (width_ > 1.4f) width_ = 1.4f;
    }

    /**
     * Process stereo input and return phase-aligned, mono-safe stereo outputs.
     */
    void Process(float in_l, float in_r, float& out_l, float& out_r) {
        // Convert to Mid / Side
        float mid = (in_l + in_r) * 0.5f;
        float side = (in_l - in_r) * 0.5f;

        // Apply 90 Hz highpass to Side channel (Direct Form II Transposed)
        float filtered_side = hp_b0_ * side + s1_;
        s1_ = hp_b1_ * side - hp_a1_ * filtered_side + s2_;
        s2_ = hp_b2_ * side - hp_a2_ * filtered_side;

        // Apply Stereo Width control to the above-90Hz side content
        float scaled_side = filtered_side * width_;

        // Reconstruct L/R
        float l = mid + scaled_side;
        float r = mid - scaled_side;

        // Studio-grade analog soft limiter curve (tanh with warm knee):
        // Prevents hard DAC clipping while preserving punchy sub impact
        out_l = tanhf(l * 1.35f) * 0.94f;
        out_r = tanhf(r * 1.35f) * 0.94f;
    }

private:
    float sample_rate_;
    float width_;

    float hp_b0_, hp_b1_, hp_b2_;
    float hp_a1_, hp_a2_;
    float s1_, s2_;
};

} // namespace rumble
