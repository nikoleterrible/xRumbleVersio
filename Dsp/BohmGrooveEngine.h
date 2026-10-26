#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace rumble {

/**
 * StateVariableFilter: 2-Pole (12 dB/oct) analog-modeled State Variable Filter
 * (Chamberlin / Trapezoidal style) with excellent numerical stability at low frequencies.
 */
class StateVariableFilter {
public:
    StateVariableFilter() { Reset(); }

    void Reset() {
        low_ = 0.0f;
        band_ = 0.0f;
    }

    void SetParameters(float fc, float q, float sample_rate) {
        // Clamp fc to safe margin
        fc = std::max(20.0f, std::min(fc, sample_rate * 0.40f));
        q  = std::max(0.5f,  std::min(q, 10.0f));

        // Chamberlin SVF tuning coefficients
        f_ = 2.0f * sinf(3.1415926535f * fc / sample_rate);
        q_inv_ = 1.0f / q;
    }

    float ProcessLowpass(float in) {
        // 2x oversampled internal integration for rock-solid stability with high Q
        for (int k = 0; k < 2; k++) {
            low_  += (f_ * 0.5f) * band_;
            float high = in - low_ - q_inv_ * band_;
            band_ += (f_ * 0.5f) * high;
        }
        return low_;
    }

    float ProcessBandpass(float in) {
        for (int k = 0; k < 2; k++) {
            low_  += (f_ * 0.5f) * band_;
            float high = in - low_ - q_inv_ * band_;
            band_ += (f_ * 0.5f) * high;
        }
        return band_;
    }

private:
    float low_;
    float band_;
    float f_;
    float q_inv_;
};

/**
 * CascadedSVF4Pole: 24 dB/oct resonant lowpass filter composed of two cascaded SVF sections.
 * Gives a punchy, analog-style resonance sweep from 30 Hz sub-bass to bright mid punch (3200 Hz).
 */
class CascadedSVF4Pole {
public:
    void Reset() {
        svf1_.Reset();
        svf2_.Reset();
    }

    void SetParameters(float fc, float q, float sample_rate) {
        // First stage: smooth Butterworth damping (Q = 0.7071)
        svf1_.SetParameters(fc, 0.7071f, sample_rate);
        // Second stage: user-controlled resonance Q (0.7 to 6.5)
        svf2_.SetParameters(fc, q, sample_rate);
    }

    float Process(float in) {
        float s1 = svf1_.ProcessLowpass(in);
        return svf2_.ProcessLowpass(s1);
    }

private:
    StateVariableFilter svf1_;
    StateVariableFilter svf2_;
};

/**
 * Biquad: Direct Form II transposed biquad for 25Hz subsonic filter and 90Hz side cut
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
 * AllpassBlock: Single channel allpass diffuser block
 */
template <size_t kMaxSize>
class AllpassBlock {
public:
    void Init(size_t delay_len, float coeff) {
        delay_len_ = (delay_len < kMaxSize) ? delay_len : kMaxSize - 1;
        coeff_ = coeff;
        Clear();
    }

    void Clear() {
        std::fill(buf_, buf_ + kMaxSize, 0.0f);
        idx_ = 0;
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
 * DampedCombBlock: Comb filter with 1-pole lowpass feedback damping and freeze support
 */
template <size_t kMaxSize>
class DampedCombBlock {
public:
    void Init(size_t delay_len, float damp_coeff) {
        delay_len_ = (delay_len < kMaxSize) ? delay_len : kMaxSize - 1;
        damp_ = damp_coeff;
        Clear();
    }

    void Clear() {
        std::fill(buf_, buf_ + kMaxSize, 0.0f);
        filter_state_ = 0.0f;
        idx_ = 0;
    }

    float Process(float in, float feedback) {
        float buf_out = buf_[idx_];
        // Lowpass damping in feedback loop
        filter_state_ = buf_out * (1.0f - damp_) + filter_state_ * damp_;
        feedback = std::min(0.998f, feedback);
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
 * BohmGrooveEngine v2.3:
 * 1. Matrice des 3 Tranches avec DOWNBEAT au centre (Tone) :
 *    - Tone (Knob 2)  = DOWNBEAT Level (Temps 1 / Sub du kick, au centre).
 *    - Blend (Knob 0) = TAP 1 Level (16e note #2, pan gauche).
 *    - Regen (Knob 1) = TAP 2 Level (16e note #3 / Offbeat, pan droite).
 *    - SILENCE ABSOLU STRICT : Deadband < 8% -> 0.000000 immédiat si les 3 sont à zéro !
 * 2. Horloge Calée sur le Jack FSU (Clock Gate Externe) :
 *    - Fini la détection audio approximative : synchronisation pure sur les pulses d'horloge.
 *    - Switch 1 (Ajustement Décalage / Micro-timing) :
 *        * UP (0)     : Ahead / Rush (-12 ms, groove tiré en avant)
 *        * CENTER (1) : On-Beat (0 ms, calage parfait)
 *        * DOWN (2)   : Laid-Back Drag (+18 ms, retard chaloupé / swing techno)
 * 3. Filtre Résonant SVF 24 dB/oct Ultra-Audible (Speed & Index) :
 *    - Speed (Knob 5) : Balayage étendu de 30 Hz à 3200 Hz (ouvert / fermé dramatique).
 *    - Index (Knob 4) : Résonance Q de 0.707 à 6.5 (sifflement acide et mordant).
 * 4. Bouton FSU Décorrélé : Effet Freeze Wash (Reverb + Delay Infini) sur pression manuelle.
 */
class BohmGrooveEngine {
public:
    static constexpr size_t kBufferMask = 32767;
    static constexpr size_t kBufferSize = 32768; // ~682ms at 48kHz

    BohmGrooveEngine()
        : sample_rate_(48000.0f), beat_interval_(21818), phase_samples_(0),
          write_idx_(0), pre_lp_state_(0.0f), pre_lp_coeff_(0.06f),
          tap1_level_(0.0f), downbeat_level_(0.8f), tap2_level_(0.8f),
          filter_cutoff_(220.0f), filter_q_(1.2f),
          reverb_size_(0.0f), drive_(1.5f),
          timing_offset_mode_(1),
          current_sat_mode_(1), prev_sat_mode_(1), sat_crossfade_(1.0f),
          fsu_freeze_active_(false), fsu_freeze_env_(0.0f), was_silent_(false)
    {
    }

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        beat_interval_ = static_cast<uint32_t>(sample_rate_ * (60.0f / 132.0f));
        phase_samples_ = 0;
        write_idx_ = 0;
        was_silent_ = false;

        std::fill(delay_buf_, delay_buf_ + kBufferSize, 0.0f);

        // Pre-filter: Gentle lowpass at 500 Hz (suppresses sharp click while preserving punch & warmth)
        pre_lp_coeff_ = 1.0f - expf(-2.0f * 3.1415926535f * 500.0f / sample_rate_);
        pre_lp_state_ = 0.0f;

        tap1_level_     = 0.0f;
        downbeat_level_ = 0.8f;
        tap2_level_     = 0.8f;

        filter_cutoff_ = 220.0f;
        filter_q_      = 1.2f;
        reverb_size_   = 0.0f;
        drive_         = 1.5f;
        timing_offset_mode_ = 1; // Center: 0ms offset
        current_sat_mode_   = 1;
        prev_sat_mode_      = 1;
        sat_crossfade_      = 1.0f;

        fsu_freeze_active_ = false;
        fsu_freeze_env_    = 0.0f;

        // SubReverb allpass diffusers
        ap1_.Init(149, 0.70f);
        ap2_.Init(223, 0.70f);
        ap3_.Init(367, 0.65f);
        ap4_.Init(521, 0.60f);

        // Damped comb loops
        comb_l1_.Init(887,  0.35f);
        comb_l2_.Init(1181, 0.35f);
        comb_r1_.Init(1013, 0.35f);
        comb_r2_.Init(1327, 0.35f);

        // 25 Hz Subsonic Highpass
        UpdateSubHP(25.0f);

        // 90 Hz Side Highpass (Mono Sub <90Hz)
        UpdateSideHP(90.0f);

        // 4-Pole Cascaded SVF (30 Hz to 3200 Hz)
        svf_l_.Reset();
        svf_r_.Reset();
        UpdateSVFFilter(220.0f, 1.2f);
    }

    void SetParameters(float k_blend, float k_tone, float k_regen,
                       float k_speed, float k_index, float k_size, float k_dense,
                       int sw_offset, int sw_sat,
                       uint32_t beat_interval, bool fsu_button_pressed)
    {
        // 1. Matrice des 3 Tranches avec Seuil Strict de Coupure à 8% (<0.08f = 0.000000)
        // Élimine tout bruit ou jitter ADC au minimum pour un silence parfait
        auto MapLevel = [](float k) -> float {
            if (k < 0.08f) return 0.0f; // Silence absolu garanti
            float norm = (k - 0.08f) / 0.92f;
            return norm * norm; // Progression dynamique propre jusqu'à 1.0 max
        };

        tap1_level_     = MapLevel(k_blend); // Blend (KNOB_0) : Tap 1 (16e #2)
        downbeat_level_ = MapLevel(k_tone);  // Tone  (KNOB_2) : Downbeat (Temps 1)
        tap2_level_     = MapLevel(k_regen); // Regen (KNOB_1) : Tap 2 / Offbeat

        // 2. Filtre Résonant SVF Ultra-Audible (Speed & Index)
        // Speed (KNOB_5): Cutoff exponentiel de 30 Hz à 3200 Hz (sweep massif et tranché!)
        float fc = 30.0f * powf(3200.0f / 30.0f, powf(k_speed, 1.25f));
        // Index (KNOB_4): Resonance Q de 0.707 (feutré) à 6.5 (auto-oscillant et sifflement acide)
        float q = 0.7071f + powf(k_index, 1.40f) * 5.8f;
        UpdateSVFFilter(fc, q);

        // 3. Reverb Size (KNOB_3): 0.0 = Taps secs percutants, 1.0 = Grand espace warehouse
        reverb_size_ = k_size;

        // 4. Dense / Drive (KNOB_6): 1.0x à 4.2x avec compensation unitaire de volume
        drive_ = 1.0f + powf(k_dense, 1.30f) * 3.2f;

        // 5. Switch 1 : Ajustement Décalage / Micro-timing
        timing_offset_mode_ = sw_offset;

        // 6. Anti-Click Switch 0 (Saturation) Crossfade
        if (sw_sat != current_sat_mode_) {
            prev_sat_mode_ = current_sat_mode_;
            current_sat_mode_ = sw_sat;
            sat_crossfade_ = 0.0f; // Start 15ms crossfade
        }

        // 7. Bouton FSU : Freeze Wash
        fsu_freeze_active_ = fsu_button_pressed;

        if (beat_interval >= 2000 && beat_interval <= 160000) {
            beat_interval_ = beat_interval;
        }
    }

    void TriggerSync() {
        phase_samples_ = 0;
    }

    void Process(float in_mono, float& out_l, float& out_r) {
        // 1. RÈGLE D'OR : Silence strict si les 3 potards du haut sont à zéro !
        bool all_muted = (tap1_level_ <= 0.0f && downbeat_level_ <= 0.0f && tap2_level_ <= 0.0f);
        if (all_muted && !fsu_freeze_active_ && fsu_freeze_env_ <= 0.001f) {
            if (!was_silent_) {
                ResetBuffers();
                was_silent_ = true;
            }
            out_l = 0.0f;
            out_r = 0.0f;
            return;
        }
        was_silent_ = false;

        float beat_f = static_cast<float>(beat_interval_);

        // Phase tracking across quarter note [0.0, 1.0)
        float phase = static_cast<float>(phase_samples_) / beat_f;
        phase_samples_++;
        if (phase_samples_ >= beat_interval_) {
            phase_samples_ = 0;
        }

        // Lowpass input conditioner (500 Hz)
        pre_lp_state_ += pre_lp_coeff_ * (in_mono - pre_lp_state_);
        float sub_in = pre_lp_state_;

        // Circular delay buffer write
        delay_buf_[write_idx_ & kBufferMask] = sub_in;
        write_idx_ = (write_idx_ + 1) & kBufferMask;

        // FSU Freeze Envelope (attack 30ms, release 350ms)
        float target_freeze = fsu_freeze_active_ ? 1.0f : 0.0f;
        float freeze_coeff = fsu_freeze_active_ ? 0.002f : 0.0002f;
        fsu_freeze_env_ += (target_freeze - fsu_freeze_env_) * freeze_coeff;

        // 2. Micro-timing Offset (Switch 1 : Ajustement Décalage)
        // Mode 0 (UP)     : Ahead / Rush (-12 ms, tire en avant)
        // Mode 1 (CENTER) : Exact On-Beat (0 ms)
        // Mode 2 (DOWN)   : Laid-Back Drag (+18 ms, retard chaloupé / swing)
        float offset_samples = 0.0f;
        if (timing_offset_mode_ == 0) {
            offset_samples = -0.012f * sample_rate_; // -12 ms
        } else if (timing_offset_mode_ == 2) {
            offset_samples = 0.018f * sample_rate_;  // +18 ms
        }

        // Taps fixes calés sur les divisions d'horloge avec offset :
        float d_downbeat = std::max(64.0f, beat_f * 1.000f + offset_samples); // Temps 1 (Downbeat)
        float d_tap1     = std::max(64.0f, beat_f * 0.250f + offset_samples); // 16e #2
        float d_tap2     = std::max(64.0f, beat_f * 0.500f + offset_samples); // 16e #3 (Offbeat)

        // 3. Lecture des 3 Tranches Indépendantes
        float t_downbeat = (downbeat_level_ > 0.0f) ? ReadTap(d_downbeat) * downbeat_level_ : 0.0f;
        float t_tap1     = (tap1_level_     > 0.0f) ? ReadTap(d_tap1)     * tap1_level_     : 0.0f;
        float t_tap2     = (tap2_level_     > 0.0f) ? ReadTap(d_tap2)     * tap2_level_     : 0.0f;

        // Panoramique spatial des 3 tranches :
        // - Downbeat (Tone)  : Centre parfait (0.707 L, 0.707 R)
        // - Tap 1 (Blend)    : Côté gauche (0.85 L, 0.15 R)
        // - Tap 2 (Regen)    : Côté droit  (0.15 L, 0.85 R)
        float mix_l = t_downbeat * 0.707f + t_tap1 * 0.85f + t_tap2 * 0.15f;
        float mix_r = t_downbeat * 0.707f + t_tap1 * 0.15f + t_tap2 * 0.85f;

        // 4. Reverb Sub & FSU Freeze Wash
        float fx_in_l, fx_in_r;
        if (reverb_size_ < 0.04f && fsu_freeze_env_ < 0.01f) {
            // Bypass sec total : taps 100% percutants et clairs
            fx_in_l = mix_l;
            fx_in_r = mix_r;
        } else {
            float mono_mix = (mix_l + mix_r) * 0.5f;
            float diff = ap1_.Process(mono_mix);
            diff = ap2_.Process(diff);
            diff = ap3_.Process(diff);
            diff = ap4_.Process(diff);

            // Reverb feedback normal : 0.20 à 0.75.
            // Si FSU Freeze actif : feedback poussé à 0.995 (Freeze Wash infini)
            float normal_fb = 0.20f + reverb_size_ * 0.55f;
            float rev_fb = normal_fb * (1.0f - fsu_freeze_env_) + 0.995f * fsu_freeze_env_;

            float l1 = comb_l1_.Process(diff, rev_fb);
            float l2 = comb_l2_.Process(diff, rev_fb);
            float r1 = comb_r1_.Process(diff, rev_fb);
            float r2 = comb_r2_.Process(diff, rev_fb);

            float rev_l = (l1 + l2) * 0.5f;
            float rev_r = (r1 + r2) * 0.5f;

            float wet = std::min(1.0f, reverb_size_ + fsu_freeze_env_ * 0.85f);
            fx_in_l = mix_l * (1.0f - 0.35f * wet) + rev_l * (1.0f * wet);
            fx_in_r = mix_r * (1.0f - 0.35f * wet) + rev_r * (1.0f * wet);
        }

        // 5. Saturation avec Gain Staging Unitaire et Transition Lisse
        if (sat_crossfade_ < 1.0f) {
            sat_crossfade_ = std::min(1.0f, sat_crossfade_ + (1.0f / (sample_rate_ * 0.015f)));
        }

        auto ComputeSaturation = [this](float x, int mode) -> float {
            float xd = x * drive_;
            if (mode == 0) {
                // Mode 0: Sub Pur (Clean soft tanh, préserve la fondamentale, compensation unitaire)
                float norm = 1.0f / sqrtf(0.8f + 0.4f * drive_);
                return tanhf(xd) * norm;
            } else if (mode == 1) {
                // Mode 1: Warm Tape (Harmoniques 2 et 3 chaleureuses, grain analogique)
                float asym = xd + 0.25f * (xd * xd);
                float norm = 1.0f / sqrtf(0.85f + 0.50f * drive_);
                return tanhf(asym) * norm;
            } else {
                // Mode 2: Fat Crunch (Bite diode et compression mordante)
                float driven = xd * 1.15f;
                float crunch;
                if (driven > 0.85f) {
                    crunch = 0.85f + 0.15f * tanhf((driven - 0.85f) * 2.0f);
                } else if (driven < -0.75f) {
                    crunch = -0.75f + 0.15f * tanhf((driven + 0.75f) * 2.0f);
                } else {
                    crunch = driven - 0.18f * (driven * driven * driven);
                }
                float norm = 1.0f / (0.90f + 0.35f * drive_);
                return crunch * norm;
            }
        };

        float sat_cur_l = ComputeSaturation(fx_in_l, current_sat_mode_);
        float sat_cur_r = ComputeSaturation(fx_in_r, current_sat_mode_);

        float sat_l, sat_r;
        if (sat_crossfade_ < 1.0f) {
            float sat_prev_l = ComputeSaturation(fx_in_l, prev_sat_mode_);
            float sat_prev_r = ComputeSaturation(fx_in_r, prev_sat_mode_);
            sat_l = (1.0f - sat_crossfade_) * sat_prev_l + sat_crossfade_ * sat_cur_l;
            sat_r = (1.0f - sat_crossfade_) * sat_prev_r + sat_crossfade_ * sat_cur_r;
        } else {
            sat_l = sat_cur_l;
            sat_r = sat_cur_r;
        }

        // 6. Filtre Résonant 4-Pole SVF (24 dB/oct, 30 Hz à 3200 Hz)
        float hp_l = hp_sub_l_.Process(sat_l);
        float hp_r = hp_sub_r_.Process(sat_r);

        // SVF 24 dB/oct sweep (spectaculaire et ultra-audible!)
        float flt_l = svf_l_.Process(hp_l);
        float flt_r = svf_r_.Process(hp_r);

        // 7. Dynamic Sidechain Ducking
        float normal_duck = 1.0f;
        if (phase < 0.08f) {
            normal_duck = 0.0f;
        } else if (phase < 0.24f) {
            float p = (phase - 0.08f) / 0.16f;
            normal_duck = p * p;
        }
        if (phase > 0.94f) {
            float p = (1.0f - phase) / 0.06f;
            normal_duck *= (p * p);
        }

        // FSU freeze adoucit le ducking pour un gros swell
        float duck = normal_duck * (1.0f - fsu_freeze_env_ * 0.70f) + 0.70f * fsu_freeze_env_;

        // Niveau unitaire propre
        float ducked_l = flt_l * duck;
        float ducked_r = flt_r * duck;

        // 8. Mid/Side Protection Mono (< 90 Hz mono)
        float mid  = (ducked_l + ducked_r) * 0.5f;
        float side = (ducked_l - ducked_r) * 0.5f;
        float filt_side = hp_side_.Process(side);

        // Limiteur de sécurité transparent à 0.95 FS
        out_l = tanhf(mid + filt_side) * 0.95f;
        out_r = tanhf(mid - filt_side) * 0.95f;
    }

    float GetCurrentPhase() const {
        return static_cast<float>(phase_samples_) / static_cast<float>(beat_interval_);
    }

    float GetDownbeatLevel() const { return downbeat_level_; }
    float GetTap1Level() const { return tap1_level_; }
    float GetTap2Level() const { return tap2_level_; }
    bool  IsFsuActive() const { return fsu_freeze_active_ || fsu_freeze_env_ > 0.05f; }

private:
    float ReadTap(float delay_samples) const {
        float rpos = static_cast<float>(write_idx_) - delay_samples;
        while (rpos < 0.0f) rpos += static_cast<float>(kBufferSize);
        while (rpos >= static_cast<float>(kBufferSize)) rpos -= static_cast<float>(kBufferSize);
        size_t i0 = static_cast<size_t>(rpos);
        size_t i1 = (i0 + 1) & kBufferMask;
        float frac = rpos - static_cast<float>(i0);
        return delay_buf_[i0] + frac * (delay_buf_[i1] - delay_buf_[i0]);
    }

    void ResetBuffers() {
        ap1_.Clear();
        ap2_.Clear();
        ap3_.Clear();
        ap4_.Clear();
        comb_l1_.Clear();
        comb_l2_.Clear();
        comb_r1_.Clear();
        comb_r2_.Clear();
        svf_l_.Reset();
        svf_r_.Reset();
        hp_sub_l_.Reset();
        hp_sub_r_.Reset();
        hp_side_.Reset();
    }

    void UpdateSVFFilter(float fc, float q) {
        filter_cutoff_ = fc;
        filter_q_ = q;
        svf_l_.SetParameters(fc, q, sample_rate_);
        svf_r_.SetParameters(fc, q, sample_rate_);
    }

    void UpdateSubHP(float freq) {
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

        hp_sub_l_.SetCoefficients(b0, b1, b2, a1, a2);
        hp_sub_r_.SetCoefficients(b0, b1, b2, a1, a2);
    }

    void UpdateSideHP(float freq) {
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

        hp_side_.SetCoefficients(b0, b1, b2, a1, a2);
    }

    float sample_rate_;
    uint32_t beat_interval_;
    uint32_t phase_samples_;
    size_t write_idx_;

    // Delay buffer
    float delay_buf_[kBufferSize];

    // Pre-filtering
    float pre_lp_state_;
    float pre_lp_coeff_;

    // Parameters
    float tap1_level_;
    float downbeat_level_;
    float tap2_level_;
    float filter_cutoff_;
    float filter_q_;
    float reverb_size_;
    float drive_;
    int timing_offset_mode_;

    // Crossfaded Saturation
    int current_sat_mode_;
    int prev_sat_mode_;
    float sat_crossfade_;

    // FSU Freeze Wash
    bool fsu_freeze_active_;
    float fsu_freeze_env_;
    bool was_silent_;

    // SubReverb Blocks
    AllpassBlock<256> ap1_;
    AllpassBlock<384> ap2_;
    AllpassBlock<512> ap3_;
    AllpassBlock<768> ap4_;

    DampedCombBlock<1024> comb_l1_;
    DampedCombBlock<1536> comb_l2_;
    DampedCombBlock<1280> comb_r1_;
    DampedCombBlock<1792> comb_r2_;

    // 4-Pole 24 dB/oct Cascaded State Variable Filters
    CascadedSVF4Pole svf_l_;
    CascadedSVF4Pole svf_r_;

    // Subsonic 25 Hz Highpass Filter
    Biquad hp_sub_l_;
    Biquad hp_sub_r_;

    // Side 90 Hz Highpass Filter
    Biquad hp_side_;
};

} // namespace rumble
