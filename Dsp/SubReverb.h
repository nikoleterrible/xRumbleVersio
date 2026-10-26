#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace rumble {

/**
 * AllpassFilter: Single channel allpass diffuser block
 */
template <size_t kMaxSize>
class Allpass {
public:
    void Init(size_t delay_len, float coeff) {
        delay_len_ = (delay_len < kMaxSize) ? delay_len : kMaxSize - 1;
        coeff_ = coeff;
        idx_ = 0;
        std::fill(buf_, buf_ + kMaxSize, 0.0f);
    }

    float Process(float in) {
        float buf_out = buf_[idx_];
        float vn = in - coeff_ * buf_out;
        float out = buf_out + coeff_ * vn;
        buf_[idx_] = vn;
        idx_ = (idx_ + 1) % delay_len_;
        return out;
    }

private:
    float buf_[kMaxSize];
    size_t idx_;
    size_t delay_len_;
    float coeff_;
};

/**
 * CombFilter: Comb delay line with 1-pole lowpass damping in feedback loop
 */
template <size_t kMaxSize>
class DampedComb {
public:
    void Init(size_t delay_len, float damp_coeff) {
        delay_len_ = (delay_len < kMaxSize) ? delay_len : kMaxSize - 1;
        damp_ = damp_coeff;
        filter_state_ = 0.0f;
        idx_ = 0;
        std::fill(buf_, buf_ + kMaxSize, 0.0f);
    }

    float Process(float in, float feedback) {
        float buf_out = buf_[idx_];
        // 1-pole lowpass filter for damping
        filter_state_ = buf_out * (1.0f - damp_) + filter_state_ * damp_;
        buf_[idx_] = in + filter_state_ * feedback;
        idx_ = (idx_ + 1) % delay_len_;
        return buf_out;
    }

private:
    float buf_[kMaxSize];
    size_t idx_;
    size_t delay_len_;
    float damp_;
    float filter_state_;
};

/**
 * SubReverb:
 * Reverb engine purpose-built for Techno Sub Rumble:
 * - Ultra-dense initial diffusion to eliminate metallic pinging
 * - Heavy high-frequency absorption
 * - Strict sub mono-collapse below 100 Hz
 * - Decay range bounded between 200 ms and 850 ms
 */
class SubReverb {
public:
    SubReverb() : sample_rate_(48000.0f), decay_feedback_(0.5f) {}

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;

        // Diffusion allpass filters (tuned prime delay lengths)
        ap1_.Init(149, 0.75f);
        ap2_.Init(223, 0.75f);
        ap3_.Init(367, 0.68f);
        ap4_.Init(521, 0.62f);

        // Parallel comb lines (tuned for low modal density without high pitch ringing)
        comb_l1_.Init(887,  0.45f);
        comb_l2_.Init(1181, 0.45f);
        comb_r1_.Init(1013, 0.45f);
        comb_r2_.Init(1327, 0.45f);

        decay_feedback_ = 0.6f;

        // Sub mono-collapse filter (highpass on the Side signal)
        // Highpass at ~110 Hz:
        float omega = 2.0f * 3.1415926535f * 110.0f / sample_rate_;
        hp_coeff_ = expf(-omega);
        side_hp_prev_in_ = 0.0f;
        side_hp_prev_out_ = 0.0f;
    }

    /**
     * @param size_knob: 0.0 (short punchy rumble ~200ms) to 1.0 (long rolling rumble ~800ms)
     */
    void SetDecay(float size_knob) {
        // Feedback mapped between 0.35 and 0.82
        decay_feedback_ = 0.35f + size_knob * 0.47f;
    }

    void Process(float in_l, float in_r, float& out_l, float& out_r) {
        // Sum input to mono for diffusion network
        float mono_in = (in_l + in_r) * 0.5f;

        // Run through 4-stage serial allpass diffuser
        float diff = ap1_.Process(mono_in);
        diff = ap2_.Process(diff);
        diff = ap3_.Process(diff);
        diff = ap4_.Process(diff);

        // Parallel comb filter bank
        float l1 = comb_l1_.Process(diff, decay_feedback_);
        float l2 = comb_l2_.Process(diff, decay_feedback_);
        float r1 = comb_r1_.Process(diff, decay_feedback_);
        float r2 = comb_r2_.Process(diff, decay_feedback_);

        float wet_l = (l1 + l2) * 0.5f;
        float wet_r = (r1 + r2) * 0.5f;

        // Mono collapse under 100 Hz (Mid/Side processing):
        // Mid contains the full sub bass, Side is high-passed so sub is strictly mono
        float mid = (wet_l + wet_r) * 0.5f;
        float side = (wet_l - wet_r) * 0.5f;

        // 1-pole highpass on side signal: y[n] = coeff * (y[n-1] + x[n] - x[n-1])
        float filtered_side = hp_coeff_ * (side_hp_prev_out_ + side - side_hp_prev_in_);
        side_hp_prev_in_ = side;
        side_hp_prev_out_ = filtered_side;

        out_l = mid + filtered_side;
        out_r = mid - filtered_side;
    }

private:
    float sample_rate_;
    float decay_feedback_;

    Allpass<256> ap1_;
    Allpass<384> ap2_;
    Allpass<512> ap3_;
    Allpass<768> ap4_;

    DampedComb<1024> comb_l1_;
    DampedComb<1536> comb_l2_;
    DampedComb<1280> comb_r1_;
    DampedComb<1792> comb_r2_;

    float hp_coeff_;
    float side_hp_prev_in_;
    float side_hp_prev_out_;
};

} // namespace rumble
