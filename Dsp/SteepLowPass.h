#pragma once
#include <cmath>

namespace rumble {

/**
 * BiquadFilter:
 * Direct Form II transposed biquad section
 */
class Biquad {
public:
    Biquad() { Reset(); }

    void Reset() {
        s1_ = 0.0f;
        s2_ = 0.0f;
    }

    void SetCoefficients(float b0, float b1, float b2, float a1, float a2) {
        b0_ = b0;
        b1_ = b1;
        b2_ = b2;
        a1_ = a1;
        a2_ = a2;
    }

    float Process(float in) {
        float out = b0_ * in + s1_;
        s1_ = b1_ * in - a1_ * out + s2_;
        s2_ = b2_ * in - a2_ * out;
        return out;
    }

private:
    float b0_, b1_, b2_, a1_, a2_;
    float s1_, s2_;
};

/**
 * SteepLowPass:
 * 4-pole (24 dB/oct) Lowpass Filter tuned specifically for Techno Sub.
 * Cuts everything above 60–180 Hz with steep roll-off.
 * Also includes an integrated 28 Hz 2-pole Butterworth Highpass to eliminate infrasonic DC mud.
 */
class SteepLowPass {
public:
    SteepLowPass() : sample_rate_(48000.0f), cutoff_hz_(110.0f) {}

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        lp_stage1_.Reset();
        lp_stage2_.Reset();
        hp_sub_cut_.Reset();

        // Initialize 28 Hz Highpass (Butterworth 2-pole)
        UpdateHighpass(28.0f);
        // Initialize Lowpass default at 110 Hz
        UpdateLowpass(110.0f);
    }

    /**
     * @param tone_knob: 0.0 (55 Hz, ultra-deep sub) to 1.0 (185 Hz, punchy low-mids)
     */
    void SetCutoff(float tone_knob) {
        // Logarithmic frequency mapping from 55 Hz to 185 Hz
        float freq = 55.0f * powf(185.0f / 55.0f, tone_knob);
        if (fabsf(freq - cutoff_hz_) > 0.5f) {
            cutoff_hz_ = freq;
            UpdateLowpass(cutoff_hz_);
        }
    }

    float Process(float in) {
        // 1) Infrasonic highpass cut (protect amplifiers and subwoofers)
        float hp = hp_sub_cut_.Process(in);

        // 2) Cascaded 4-pole steep lowpass (2 x 12 dB/oct = 24 dB/oct)
        float lp1 = lp_stage1_.Process(hp);
        float lp2 = lp_stage2_.Process(lp1);

        return lp2;
    }

    float GetCutoffHz() const { return cutoff_hz_; }

private:
    void UpdateLowpass(float cutoff) {
        // Cascaded Butterworth 4-pole: Q1 = 0.5412, Q2 = 1.3065
        ConfigureLPF(lp_stage1_, cutoff, 0.65f);
        ConfigureLPF(lp_stage2_, cutoff, 1.15f); // Slight bump at cutoff for weight
    }

    void ConfigureLPF(Biquad& stage, float freq, float q) {
        float omega = 2.0f * 3.1415926535f * freq / sample_rate_;
        float sin_w = sinf(omega);
        float cos_w = cosf(omega);
        float alpha = sin_w / (2.0f * q);

        float a0 = 1.0f + alpha;
        float b0 = ((1.0f - cos_w) * 0.5f) / a0;
        float b1 = (1.0f - cos_w) / a0;
        float b2 = ((1.0f - cos_w) * 0.5f) / a0;
        float a1 = (-2.0f * cos_w) / a0;
        float a2 = (1.0f - alpha) / a0;

        stage.SetCoefficients(b0, b1, b2, a1, a2);
    }

    void UpdateHighpass(float freq) {
        float omega = 2.0f * 3.1415926535f * freq / sample_rate_;
        float sin_w = sinf(omega);
        float cos_w = cosf(omega);
        float alpha = sin_w / (2.0f * 0.7071f);

        float a0 = 1.0f + alpha;
        float b0 = ((1.0f + cos_w) * 0.5f) / a0;
        float b1 = (-(1.0f + cos_w)) / a0;
        float b2 = ((1.0f + cos_w) * 0.5f) / a0;
        float a1 = (-2.0f * cos_w) / a0;
        float a2 = (1.0f - alpha) / a0;

        hp_sub_cut_.SetCoefficients(b0, b1, b2, a1, a2);
    }

    float sample_rate_;
    float cutoff_hz_;
    Biquad lp_stage1_;
    Biquad lp_stage2_;
    Biquad hp_sub_cut_;
};

} // namespace rumble
