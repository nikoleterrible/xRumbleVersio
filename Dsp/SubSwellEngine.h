#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace rumble {

/**
 * SubSwellEngine:
 * True Techno Rumble Engine based on Sub Cavity Resonance and Phase-Locked Swell Envelopes.
 * 
 * Replaces discrete clicky delay echoes with a continuous, deep, breathing sub-bass wash
 * sculpted by smooth "Gonflement" (Swell) envelopes:
 * 
 * - 0% Kick Attack: Input send is 4-pole low-passed at 75 Hz (zero beater click or snap)
 * - Continuous modal sub resonance (48 - 60 Hz)
 * - Smooth Swell Envelopes:
 *     * Berlin Offbeat Swell (slow S-curve blooming on the offbeat)
 *     * Rolling 16th Wave Train (3 smooth undulating sub waves on steps 2, 3, 4)
 *     * Triplet Gallop (hypnotic 12-T undulations)
 * - Anticipatory 8% Tuck-in: Ramps down to 0.0 before the next kick hits (ZERO click on kick onset)
 * - Performance Roll mode on FSU hold (accelerated 32nd-note build-up swell)
 */
class SubSwellEngine {
public:
    SubSwellEngine() 
        : sample_rate_(48000.0f), beat_interval_(21818), phase_samples_(0),
          groove_shape_(0.25f), length_(0.65f), swing_(0.0f), spread_(0.85f),
          roll_active_(false), roll_phase_(0.0f),
          c1_idx_(0), c2_idx_(0), c3_idx_(0),
          c1_filt_(0.0f), c2_filt_(0.0f), c3_filt_(0.0f),
          ap1_idx_(0), ap2_idx_(0)
    {
    }

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        beat_interval_ = static_cast<uint32_t>(sample_rate_ * (60.0f / 132.0f));
        phase_samples_ = 0;
        groove_shape_ = 0.25f;
        length_ = 0.65f;
        swing_ = 0.0f;
        spread_ = 0.85f;
        roll_active_ = false;
        roll_phase_ = 0.0f;

        // Comb cavity delays tuned to sub frequencies (48 Hz, 54 Hz, 64 Hz)
        c1_len_ = static_cast<size_t>(sample_rate_ / 48.0f);
        c2_len_ = static_cast<size_t>(sample_rate_ / 54.0f);
        c3_len_ = static_cast<size_t>(sample_rate_ / 64.0f);
        if (c1_len_ > kMaxCombLen) c1_len_ = kMaxCombLen;
        if (c2_len_ > kMaxCombLen) c2_len_ = kMaxCombLen;
        if (c3_len_ > kMaxCombLen) c3_len_ = kMaxCombLen;

        std::fill(c1_buf_, c1_buf_ + kMaxCombLen, 0.0f);
        std::fill(c2_buf_, c2_buf_ + kMaxCombLen, 0.0f);
        std::fill(c3_buf_, c3_buf_ + kMaxCombLen, 0.0f);
        c1_idx_ = 0; c2_idx_ = 0; c3_idx_ = 0;
        c1_filt_ = 0.0f; c2_filt_ = 0.0f; c3_filt_ = 0.0f;

        std::fill(ap1_buf_, ap1_buf_ + 241, 0.0f);
        std::fill(ap2_buf_, ap2_buf_ + 373, 0.0f);
        ap1_idx_ = 0; ap2_idx_ = 0;

        // 4-pole steep lowpass filter on input send at 75 Hz
        ConfigureSendLPF(75.0f);
    }

    void SetParameters(float groove_shape, float length, float swing, float spread,
                       int grid_mode, uint32_t beat_interval, bool roll_fsu)
    {
        groove_shape_ = groove_shape;
        length_ = length;
        swing_ = swing;
        spread_ = spread;
        grid_mode_ = grid_mode;
        roll_active_ = roll_fsu;

        if (beat_interval > 3000 && beat_interval < 96000) {
            beat_interval_ = beat_interval;
        }
    }

    void TriggerSync() {
        // Sample-accurate beat phase resync on detected kick or Gate In
        phase_samples_ = 0;
    }

    /**
     * Process one sample of audio input.
     * Returns stereo wet sub rumble (sig_l, sig_r).
     */
    void Process(float in_mono, float& sig_l, float& sig_r) {
        // 1. Beat phase tracking [0.0 to 1.0] across the quarter note
        float phase = static_cast<float>(phase_samples_) / static_cast<float>(beat_interval_);
        phase_samples_++;
        if (phase_samples_ >= beat_interval_) {
            phase_samples_ = 0;
        }

        // 2. 4-Pole Steep 75 Hz Pre-Filter on Kick Send
        // Eliminates 100% of kick click, beater transient, and slap
        float lp1 = send_b0_ * in_mono + s1_1_;
        s1_1_ = send_b1_ * in_mono - send_a1_ * lp1 + s2_1_;
        s2_1_ = send_b2_ * in_mono - send_a2_ * lp1;

        float lp2 = send_b0_ * lp1 + s1_2_;
        s1_2_ = send_b1_ * lp1 - send_a1_ * lp2 + s2_2_;
        s2_2_ = send_b2_ * lp1 - send_a2_ * lp2;
        float pure_sub_in = lp2;

        // 3. Resonant Sub Diffuser Cavity
        // Diffuse into continuous drone
        float diff = Allpass(pure_sub_in, ap1_buf_, 241, ap1_idx_, 0.65f);
        diff = Allpass(diff, ap2_buf_, 373, ap2_idx_, 0.60f);

        // Feedback comb resonator loops (decay length controlled by 'length_' knob)
        float fb_gain = 0.68f + length_ * 0.16f; // 0.68 to 0.84

        float c1 = c1_buf_[c1_idx_];
        c1_filt_ += 0.15f * (c1 - c1_filt_);
        c1_buf_[c1_idx_] = diff + c1_filt_ * fb_gain;
        c1_idx_ = (c1_idx_ + 1) % c1_len_;

        float c2 = c2_buf_[c2_idx_];
        c2_filt_ += 0.15f * (c2 - c2_filt_);
        c2_buf_[c2_idx_] = diff + c2_filt_ * fb_gain;
        c2_idx_ = (c2_idx_ + 1) % c2_len_;

        float c3 = c3_buf_[c3_idx_];
        c3_filt_ += 0.15f * (c3 - c3_filt_);
        c3_buf_[c3_idx_] = diff + c3_filt_ * (fb_gain * 0.96f);
        c3_idx_ = (c3_idx_ + 1) % c3_len_;

        // Continuous spacious sub cavity signal
        float cavity_l = (c1 + c2 * 0.5f) * 0.65f;
        float cavity_r = (c2 + c3 * 0.5f) * 0.65f;

        // 4. Compute Smooth Swell Envelope ("Le gonflement qui monte")
        float env = 0.0f;

        if (roll_active_) {
            // PERFORMANCE ROLL (FSU Pressed):
            // Fast 32nd-note oscillating wave swell
            roll_phase_ += 8.0f / static_cast<float>(beat_interval_);
            if (roll_phase_ >= 1.0f) roll_phase_ -= 1.0f;
            env = 0.5f * (1.0f - cosf(2.0f * 3.1415926535f * roll_phase_)) * 1.2f;
        } else {
            env = ComputeSwellEnvelope(phase);
        }

        // 5. Apply the Swell Envelope with Stereo Spatial Movement
        float pan_mod = sinf(phase * 3.1415926535f) * 0.35f * spread_;
        float gain_l = (1.0f - pan_mod) * env * 2.5f;
        float gain_r = (1.0f + pan_mod) * env * 2.5f;

        sig_l = cavity_l * gain_l;
        sig_r = cavity_r * gain_r;
    }

    float GetCurrentPhase() const {
        return static_cast<float>(phase_samples_) / static_cast<float>(beat_interval_);
    }

private:
    float Allpass(float x, float* buf, size_t len, size_t& idx, float coeff) {
        float buf_out = buf[idx];
        float vn = x - coeff * buf_out;
        float out = buf_out + coeff * vn;
        buf[idx] = vn;
        idx = (idx + 1) % len;
        return out;
    }

    void ConfigureSendLPF(float fc) {
        float w = 2.0f * 3.1415926535f * fc / sample_rate_;
        float sin_w = sinf(w);
        float cos_w = cosf(w);
        float alpha = sin_w / (2.0f * 0.7071f);
        float a0 = 1.0f + alpha;

        send_b0_ = ((1.0f - cos_w) * 0.5f) / a0;
        send_b1_ = (1.0f - cos_w) / a0;
        send_b2_ = ((1.0f - cos_w) * 0.5f) / a0;
        send_a1_ = (-2.0f * cos_w) / a0;
        send_a2_ = (1.0f - alpha) / a0;

        s1_1_ = 0.0f; s2_1_ = 0.0f;
        s1_2_ = 0.0f; s2_2_ = 0.0f;
    }

    float ComputeSwellEnvelope(float phase) {
        if (grid_mode_ == 1) {
            // --- TRIPLET HYPNOTIC SWELL (12-T) ---
            if (phase < 0.15f) {
                return 0.0f;
            } else if (phase < 0.52f) {
                float p = (phase - 0.15f) / 0.37f;
                return sinf(3.1415926535f * p) * 0.75f;
            } else if (phase < 0.93f) {
                float p = (phase - 0.52f) / 0.41f;
                return sinf(3.1415926535f * p) * 1.00f;
            } else {
                return 0.0f;
            }
        }

        // STRAIGHT / SWELL MORPHING (controlled by groove_shape_ [0.0 to 1.0]):
        if (groove_shape_ < 0.40f) {
            // --- ARCHETYPE 1: BERLIN OFFBEAT SUB SWELL (Pure Gonflement) ---
            // 0.00 - 0.10: 100% Silence (Kick passes cleanly)
            // 0.10 - 0.70: Smooth S-Curve Swell blooming into offbeat
            // 0.70 - 0.92: Resonant bloom sustain
            // 0.92 - 1.00: Smooth anticipatory tuck-in to 0.0 (ZERO boundary click)
            if (phase < 0.10f) {
                return 0.0f;
            } else if (phase < 0.70f) {
                float p = (phase - 0.10f) / 0.60f;
                return p * p * (3.0f - 2.0f * p); // Smoothstep: 0.0 to 1.0
            } else if (phase < 0.92f) {
                return 1.0f;
            } else {
                float p = (1.0f - phase) / 0.08f;
                return p * p;
            }
        } else if (groove_shape_ < 0.75f) {
            // --- ARCHETYPE 2: ROLLING 16TH WAVE TRAIN ---
            // 3 smooth undulating waves on steps 2, 3, 4 with ZERO clicks
            if (phase < 0.15f) {
                return 0.0f;
            } else if (phase < 0.40f) {
                float p = (phase - 0.15f) / 0.25f;
                return sinf(3.1415926535f * p) * 0.60f;
            } else if (phase < 0.68f) {
                float p = (phase - 0.40f) / 0.28f;
                return sinf(3.1415926535f * p) * 0.85f;
            } else if (phase < 0.94f) {
                float p = (phase - 0.68f) / 0.26f;
                return sinf(3.1415926535f * p) * 1.00f;
            } else {
                return 0.0f;
            }
        } else {
            // --- ARCHETYPE 3: SYNCOPATED DOTTED PUMP ---
            if (phase < 0.18f) {
                return 0.0f;
            } else if (phase < 0.50f) {
                float p = (phase - 0.18f) / 0.32f;
                return sinf(3.1415926535f * p) * 0.85f;
            } else if (phase < 0.92f) {
                float p = (phase - 0.50f) / 0.42f;
                return sinf(3.1415926535f * p) * 1.00f;
            } else {
                return 0.0f;
            }
        }
    }

    static constexpr size_t kMaxCombLen = 1200;

    float sample_rate_;
    uint32_t beat_interval_;
    uint32_t phase_samples_;

    float groove_shape_;
    float length_;
    float swing_;
    float spread_;
    int grid_mode_;
    bool roll_active_;
    float roll_phase_;

    // 4-pole pre-filter coefficients
    float send_b0_, send_b1_, send_b2_;
    float send_a1_, send_a2_;
    float s1_1_, s2_1_;
    float s1_2_, s2_2_;

    // Sub cavity combs
    size_t c1_len_, c2_len_, c3_len_;
    size_t c1_idx_, c2_idx_, c3_idx_;
    float c1_filt_, c2_filt_, c3_filt_;
    float c1_buf_[kMaxCombLen];
    float c2_buf_[kMaxCombLen];
    float c3_buf_[kMaxCombLen];

    // Allpass diffusers
    size_t ap1_idx_, ap2_idx_;
    float ap1_buf_[241];
    float ap2_buf_[373];
};

} // namespace rumble
