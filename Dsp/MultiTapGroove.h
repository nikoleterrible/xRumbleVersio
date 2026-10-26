#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace rumble {

/**
 * MultiTapGroove:
 * Multi-tap delay engine inspired by the Bohm: Groove expander by Ohm Force.
 * Generates 16th-note / triplet ghost-kick rumbles around the main kick.
 * 
 * Features:
 * - 4 distinct taps (Kick / Ducked, Tap 2, Tap 3, Tap 4)
 * - Continuous Groove Shape morphing across 4 techno archetypes
 * - Swing / micro-timing offset
 * - Length / decay per tap (from punchy staccato clicks to deep sustained rumble)
 * - True stereo panning per tap (L-C-R ping-pong spread)
 * - Performance Roll / Burst mode on FSU hold (32nd note acceleration)
 */
class MultiTapGroove {
public:
    static constexpr size_t kMaxDelaySamples = 32000; // ~660 ms at 48kHz (covers 1 bar down to 90 BPM)

    MultiTapGroove() 
        : sample_rate_(48000.0f), write_idx_(0), beat_interval_(22153),
          feedback_(0.15f), swing_offset_(0.0f), length_(0.5f),
          spread_(0.7f), roll_active_(false), roll_phase_(0.0f)
    {
        for (int i = 0; i < 4; i++) {
            tap_weights_[i] = 0.0f;
            tap_pans_[i] = 0.0f;
        }
    }

    void Init(float sample_rate) {
        sample_rate_ = sample_rate;
        write_idx_ = 0;
        beat_interval_ = static_cast<uint32_t>(sample_rate_ * 0.4615f); // 130 BPM
        feedback_ = 0.15f;
        swing_offset_ = 0.0f;
        length_ = 0.5f;
        spread_ = 0.7f;
        roll_active_ = false;
        roll_phase_ = 0.0f;
        filter_l_ = 0.0f;
        filter_r_ = 0.0f;
        tap_smooth_l_ = 0.0f;
        tap_smooth_r_ = 0.0f;
        std::fill(buffer_l_, buffer_l_ + kMaxDelaySamples, 0.0f);
        std::fill(buffer_r_, buffer_r_ + kMaxDelaySamples, 0.0f);
    }

    /**
     * @param shape_knob: 0.0 to 1.0 (morphs between Offbeat, Rolling 16ths, Syncopated, Triplet)
     * @param length_knob: 0.0 (short punchy ghost taps) to 1.0 (long sustained rumble)
     * @param swing_knob: 0.0 (straight) to 1.0 (groove swing & tap feedback)
     * @param spread_knob: 0.0 (mono) to 1.0 (wide stereo ping-pong)
     * @param grid_mode: 0 = 16th Straight, 1 = Triplet 12-T, 2 = Free / Drift
     * @param beat_interval_samples: tempo reference (from audio detector or gate in)
     * @param roll_fsu: true when FSU button or roll trigger is held
     */
    void SetParameters(float shape_knob, float length_knob, float swing_knob,
                       float spread_knob, int grid_mode,
                       uint32_t beat_interval_samples, bool roll_fsu) 
    {
        if (beat_interval_samples > 2000 && beat_interval_samples < kMaxDelaySamples) {
            beat_interval_ = beat_interval_samples;
        }

        length_ = length_knob;
        spread_ = spread_knob;
        roll_active_ = roll_fsu;

        // Feedback: controlled by swing/accent knob, bounded safely
        feedback_ = 0.05f + swing_knob * 0.55f;

        // Micro-timing swing offset on even 16th notes (up to 18% delay)
        swing_offset_ = swing_knob * 0.18f;

        // Calculate Tap Gains and Timings based on Groove Shape [0.0 to 1.0] and Grid Mode
        ComputeTapPattern(shape_knob, grid_mode);
    }

    /**
     * Process one sample of sub-filtered kick input.
     * Generates stereo outputs out_l and out_r.
     */
    void Process(float in_mono, float& out_l, float& out_r) {
        float beat_f = static_cast<float>(beat_interval_);

        float sig_l = 0.0f;
        float sig_r = 0.0f;

        if (roll_active_) {
            // PERFORMANCE ROLL / BURST (FSU Pressed):
            // Accelerates taps into intense 32nd-note burst with filter swell
            roll_phase_ += 1.0f;
            float tap_32 = beat_f * 0.125f; // 32nd note
            float tap_16 = beat_f * 0.250f; // 16th note

            float s1 = ReadBuffer(buffer_l_, tap_32);
            float s2 = ReadBuffer(buffer_r_, tap_16);

            // Fast stereo ping-pong oscillation
            sig_l = (s1 * 1.3f) + (s2 * 0.5f);
            sig_r = (s2 * 1.3f) + (s1 * 0.5f);
        } else {
            // NORMAL MULTI-TAP GROOVE (Bohm style):
            for (int t = 0; t < 4; t++) {
                float weight = tap_weights_[t];
                if (weight <= 0.001f) continue;

                float delay_time = tap_delays_[t];

                // Read from circular delay buffers with linear interpolation
                float tap_sample_l = ReadBuffer(buffer_l_, delay_time);
                float tap_sample_r = ReadBuffer(buffer_r_, delay_time);

                // Stereo Panning per Tap:
                // pan: -1.0 (Full Left), 0.0 (Center), +1.0 (Full Right)
                float pan = tap_pans_[t] * spread_;
                float gain_l = 0.5f * (1.0f - pan);
                float gain_r = 0.5f * (1.0f + pan);

                sig_l += tap_sample_l * weight * gain_l;
                sig_r += tap_sample_r * weight * gain_r;
            }
        }

        // Tap Length damping & smoothing filter (rounds off any sharp transient attack on taps)
        tap_smooth_l_ += 0.20f * (sig_l - tap_smooth_l_);
        tap_smooth_r_ += 0.20f * (sig_r - tap_smooth_r_);
        sig_l = tap_smooth_l_;
        sig_r = tap_smooth_r_;

        filter_l_ += 0.20f * (sig_l - filter_l_);
        filter_r_ += 0.20f * (sig_r - filter_r_);

        // Write into circular buffers with cross-feedback
        float fb_l = tanhf(filter_l_ * 1.2f) * 0.85f;
        float fb_r = tanhf(filter_r_ * 1.2f) * 0.85f;

        // Cross-feeding L/R for stereo cohesion
        buffer_l_[write_idx_] = in_mono + (fb_l * 0.7f + fb_r * 0.3f) * feedback_;
        buffer_r_[write_idx_] = in_mono + (fb_r * 0.7f + fb_l * 0.3f) * feedback_;

        write_idx_ = (write_idx_ + 1) % kMaxDelaySamples;

        out_l = sig_l;
        out_r = sig_r;
    }

    // Get current tap energy for front-panel 4x LED animation
    void GetTapEnergy(float energies[4]) const {
        for (int i = 0; i < 4; i++) {
            energies[i] = tap_weights_[i];
        }
    }

    bool IsRollActive() const { return roll_active_; }

private:
    float ReadBuffer(const float* buffer, float delay_samples) {
        if (delay_samples < 1.0f) delay_samples = 1.0f;
        if (delay_samples >= kMaxDelaySamples - 2) delay_samples = kMaxDelaySamples - 2;

        float read_pos = static_cast<float>(write_idx_) - delay_samples;
        if (read_pos < 0.0f) {
            read_pos += kMaxDelaySamples;
        }

        size_t idx0 = static_cast<size_t>(read_pos);
        size_t idx1 = (idx0 + 1) % kMaxDelaySamples;
        float frac = read_pos - static_cast<float>(idx0);

        return buffer[idx0] + frac * (buffer[idx1] - buffer[idx0]);
    }

    void ComputeTapPattern(float shape_knob, int grid_mode) {
        float beat_f = static_cast<float>(beat_interval_);

        if (grid_mode == 1) {
            // --- TRIPIET GRID (12-T) ---
            // Divisions: 1/6 beat (12th), 2/6 (6th), 4/6 (3rd)
            tap_delays_[0] = beat_f * (1.0f / 6.0f); // Tap 1
            tap_delays_[1] = beat_f * (2.0f / 6.0f); // Tap 2
            tap_delays_[2] = beat_f * (4.0f / 6.0f); // Tap 3
            tap_delays_[3] = beat_f * (5.0f / 6.0f); // Tap 4

            tap_pans_[0] = -0.6f;
            tap_pans_[1] = +0.6f;
            tap_pans_[2] = -0.3f;
            tap_pans_[3] = +0.3f;

            // Shape alters weighting between running triplet and syncopated gallop
            tap_weights_[0] = 0.5f + 0.5f * shape_knob;
            tap_weights_[1] = 0.85f;
            tap_weights_[2] = 0.95f;
            tap_weights_[3] = 0.4f + 0.6f * (1.0f - shape_knob);

        } else if (grid_mode == 2) {
            // --- FREE / ANALOG DRIFT ---
            // Continuous spacing using shape_knob (50ms to 280ms)
            float base_time = 0.050f * sample_rate_ + shape_knob * (0.230f * sample_rate_);
            tap_delays_[0] = base_time * 0.25f;
            tap_delays_[1] = base_time * 0.50f;
            tap_delays_[2] = base_time * 0.75f;
            tap_delays_[3] = base_time * 1.00f;

            tap_pans_[0] = -0.7f;
            tap_pans_[1] = +0.2f;
            tap_pans_[2] = -0.2f;
            tap_pans_[3] = +0.7f;

            tap_weights_[0] = 0.6f;
            tap_weights_[1] = 0.8f;
            tap_weights_[2] = 1.0f;
            tap_weights_[3] = 0.7f;

        } else {
            // --- 16th STRAIGHT GRID (Bohm Default) ---
            // Step 1: Main kick (handled by dry/ducking)
            // Tap 0 (16th note #2): +0.25 beat
            // Tap 1 (16th note #3 / 8th offbeat): +0.50 beat
            // Tap 2 (16th note #4): +0.75 beat
            // Tap 3 (Ghost 32nd pre-kick or double tap): +0.875 beat

            float d2 = beat_f * 0.25f * (1.0f + swing_offset_);
            float d3 = beat_f * 0.50f;
            float d4 = beat_f * 0.75f * (1.0f + swing_offset_);
            float d_ghost = beat_f * 0.875f;

            tap_delays_[0] = d2;
            tap_delays_[1] = d3;
            tap_delays_[2] = d4;
            tap_delays_[3] = d_ghost;

            // Stereo placement: Ping-pong bouncing across stereo field
            tap_pans_[0] = -0.75f; // Tap 2 panned Left
            tap_pans_[1] =  0.00f; // Tap 3 (Offbeat) solid Center
            tap_pans_[2] = +0.75f; // Tap 4 panned Right
            tap_pans_[3] = -0.35f; // Ghost panned mid-left

            // Morphing between 4 distinct Groove Archetypes:
            if (shape_knob < 0.28f) {
                // ARCHETYPE 1: Off-Beat Berlinois (Classic Berghain bounce)
                // Pure heavy 8th offbeat (Tap 3) with subtle ghost tap
                float norm = shape_knob / 0.28f;
                tap_weights_[0] = 0.15f * norm;
                tap_weights_[1] = 1.00f; // Dominant offbeat
                tap_weights_[2] = 0.35f + 0.25f * norm;
                tap_weights_[3] = 0.10f;
            } else if (shape_knob < 0.60f) {
                // ARCHETYPE 2: Rolling 16ths Driving Train (Drumcode / Industrial)
                // 2 -> 3 -> 4 rolling crescendo
                float norm = (shape_knob - 0.28f) / 0.32f;
                tap_weights_[0] = 0.15f + 0.65f * norm; // Tap 2 grows
                tap_weights_[1] = 0.90f;
                tap_weights_[2] = 0.60f + 0.40f * norm; // Tap 4 punches
                tap_weights_[3] = 0.20f * norm;
            } else if (shape_knob < 0.85f) {
                // ARCHETYPE 3: Syncopated / Dotted Gallop (UK Bass / Modern Techno)
                // Taps 2 and 4 accented, tap 3 slightly hollowed
                float norm = (shape_knob - 0.60f) / 0.25f;
                tap_weights_[0] = 0.80f;
                tap_weights_[1] = 0.90f - 0.35f * norm; // Hollow out offbeat
                tap_weights_[2] = 1.00f;
                tap_weights_[3] = 0.30f + 0.40f * norm;
            } else {
                // ARCHETYPE 4: Hypnotic Triplet Simulation (3 against 4 polyrhythm)
                float norm = (shape_knob - 0.85f) / 0.15f;
                tap_weights_[0] = 0.70f;
                tap_weights_[1] = 0.55f + 0.35f * norm;
                tap_weights_[2] = 0.90f;
                tap_weights_[3] = 0.70f;
            }
        }

        // Apply Length factor: length scales overall tap sustain and tail
        float len_mult = 0.65f + length_ * 0.45f;
        for (int i = 0; i < 4; i++) {
            tap_weights_[i] *= len_mult;
        }
    }

    float sample_rate_;
    size_t write_idx_;
    uint32_t beat_interval_;

    float feedback_;
    float swing_offset_;
    float length_;
    float spread_;
    bool roll_active_;
    float roll_phase_;

    float tap_delays_[4];
    float tap_weights_[4];
    float tap_pans_[4];

    float filter_l_;
    float filter_r_;
    float tap_smooth_l_;
    float tap_smooth_r_;

    float buffer_l_[kMaxDelaySamples];
    float buffer_r_[kMaxDelaySamples];
};

} // namespace rumble
