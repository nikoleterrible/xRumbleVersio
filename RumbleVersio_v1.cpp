#include "daisy_versio.h"
#include "Dsp/TransientDetector.h"
#include "Dsp/DuckingEnvelope.h"
#include "Dsp/RhythmicDelay.h"
#include "Dsp/SubReverb.h"
#include "Dsp/SaturationStage.h"
#include "Dsp/SteepLowPass.h"

using namespace daisy;
using namespace rumble;

// Hardware interface
static DaisyVersio hw;

// DSP Modules
static TransientDetector detector;
static DuckingEnvelope   ducking;
static RhythmicDelay     delay_stage;
static SubReverb         reverb_stage;
static SaturationStage   sat_stage_l;
static SaturationStage   sat_stage_r;
static SteepLowPass      lpf_stage_l;
static SteepLowPass      lpf_stage_r;

// Pre-filter state (strips high frequencies/click from entering reverb)
static float pre_lp_state = 0.0f;
static float pre_lp_coeff = 0.018f; // ~140 Hz at 48kHz

// Performance / Mute (FSU) state
static float fsu_gain = 1.0f;
static float fsu_target = 1.0f;

// LED rendering values
static float led_brightness = 1.0f;
static int   current_sat_mode = 1;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    // Read hardware controls
    hw.ProcessAnalogControls();
    hw.tap.Debounce();

    // Knob mappings:
    // Blend (Knob 0): Texture / Feedback in the pre-reverb delay
    float k_texture = hw.GetKnobValue(DaisyVersio::KNOB_0);
    // Tone  (Knob 1): Steep 24dB/oct LPF Cutoff (60 - 180 Hz)
    float k_tone    = hw.GetKnobValue(DaisyVersio::KNOB_1);
    // Rain  (Knob 2): Ducking Depth
    float k_depth   = hw.GetKnobValue(DaisyVersio::KNOB_2);
    // Speed (Knob 3): Drive / Saturation Amount
    float k_speed   = hw.GetKnobValue(DaisyVersio::KNOB_3);
    // Index (Knob 4): Ducking Release / Recovery Shape
    float k_release = hw.GetKnobValue(DaisyVersio::KNOB_4);
    // Size  (Knob 5): Reverb Sub Decay Length
    float k_size    = hw.GetKnobValue(DaisyVersio::KNOB_5);
    // Decay (Knob 6): Pre-delay / Groove Time
    float k_delay   = hw.GetKnobValue(DaisyVersio::KNOB_6);

    // Switches:
    // Switch 1 (ABC): Saturation Mode
    // 0 = Down (Hard Clip), 1 = Center (Warm Tape), 2 = Up (Sub Pur Clean)
    int sw1_raw = hw.sw[DaisyVersio::SW_0].Read();
    int sat_mode = (sw1_raw == 2) ? 0 : (sw1_raw == 1 ? 1 : 2);
    current_sat_mode = sat_mode;

    // Switch 2 (XYZ): Sync / Tempo Mode
    // 0 = Down (Sync Triplet), 1 = Center (Free / Unsynced), 2 = Up (Sync Straight)
    int sw2_raw = hw.sw[DaisyVersio::SW_1].Read();
    bool is_synced = (sw2_raw != 1);
    bool is_triplet = (sw2_raw == 0);

    // FSU Button & FSU Gate Input Jack: Instant Mute for drops
    // Responds to physical button press OR external gate signal (> +2V) into FSU jack
    bool fsu_active = hw.tap.Pressed() || hw.Gate();
    fsu_target = fsu_active ? 0.0f : 1.0f;

    // Beat interval reference for tempo-synced timing
    uint32_t beat_interval = is_synced ? detector.GetBeatIntervalSamples() : 0;

    // Configure DSP parameters
    ducking.SetParameters(k_depth, k_release, beat_interval);
    delay_stage.SetParameters(k_delay, k_texture, beat_interval, is_triplet);
    reverb_stage.SetDecay(k_size);
    sat_stage_l.SetParameters(k_speed, sat_mode);
    sat_stage_r.SetParameters(k_speed, sat_mode);
    lpf_stage_l.SetCutoff(k_tone);
    lpf_stage_r.SetCutoff(k_tone);

    // Process audio block
    for (size_t i = 0; i < size; i++)
    {
        float in_l = in[0][i];
        float in_r = in[1][i];
        float in_mono = (in_l + in_r) * 0.5f;

        // 1. Kick Transient Detection (Analyzes incoming kick punch)
        bool kick_detected = detector.Process(in_mono);
        if (kick_detected)
        {
            ducking.Trigger();
        }

        // 2. Pre-filter send input to remove kick click/snap (only feed dark sub < 140 Hz to reverb)
        pre_lp_state += pre_lp_coeff * (in_mono - pre_lp_state);
        float sub_send = pre_lp_state;

        // 3. Pre-Delay with Rhythmic Texture Feedback
        float delayed = delay_stage.Process(sub_send);

        // 4. Sub-Tuned Dense Reverb with Mono-Collapse
        float rev_l, rev_r;
        reverb_stage.Process(delayed, delayed, rev_l, rev_r);

        // 5. Drive & Saturation Stage
        float sat_l = sat_stage_l.Process(rev_l);
        float sat_r = sat_stage_r.Process(rev_r);

        // 6. Steep 4-pole 24 dB/oct Low-Pass + 28 Hz Sub Cut
        float lpf_l = lpf_stage_l.Process(sat_l);
        float lpf_r = lpf_stage_r.Process(sat_r);

        // 7. Smooth FSU (Mute) ramp (prevents audio pops during drops)
        fsu_gain += 0.005f * (fsu_target - fsu_gain);

        // 8. Auto-Ducking VCA
        float duck_gain = ducking.Process() * fsu_gain;
        float wet_l = lpf_l * duck_gain;
        float wet_r = lpf_r * duck_gain;

        // 9. Output Routing with Soft Limiter (prevents DAC overs/clipping crackle):
        // Out L (in series): Direct Kick Dry + 100% Rumble Wet
        float sum_mix = (in_l * 0.85f) + (wet_l * 1.30f);
        out[0][i] = tanhf(sum_mix * 0.90f) * 0.95f;

        // Out R (aux/send): 100% Pure Rumble Wet
        out[1][i] = tanhf(wet_r * 1.80f) * 0.95f;
    }

    // Save envelope for LED visualization
    led_brightness = ducking.GetEnvelopeValue() * fsu_gain;
}

int main(void)
{
    // Initialize Versio hardware with high-performance CPU boost (480 MHz)
    hw.Init(true);

    float sample_rate = hw.AudioSampleRate();
    pre_lp_coeff = 1.0f - expf(-2.0f * 3.1415926535f * 140.0f / sample_rate);

    // Initialize all DSP blocks
    detector.Init(sample_rate);
    ducking.Init(sample_rate);
    delay_stage.Init(sample_rate);
    reverb_stage.Init(sample_rate);
    sat_stage_l.Init();
    sat_stage_r.Init();
    lpf_stage_l.Init(sample_rate);
    lpf_stage_r.Init(sample_rate);

    // Block size: 16 samples for ultra-low latency (< 0.35 ms)
    hw.SetAudioBlockSize(16);

    // Start audio stream
    hw.StartAudio(AudioCallback);

    while (1)
    {
        // Smooth LED visualization reflecting ducking and saturation mode
        float r = 0.0f, g = 0.0f, b = 0.0f;

        if (fsu_target < 0.5f) {
            // Mute active: faint white warning flash
            r = 0.15f; g = 0.15f; b = 0.15f;
        } else {
            // Colors mapped to Saturation Mode:
            switch (current_sat_mode) {
                case 0: // Sub Pur: Deep Cyan / Blue
                    r = 0.05f * led_brightness;
                    g = 0.55f * led_brightness;
                    b = 1.00f * led_brightness;
                    break;
                case 1: // Warm Tape: Amber / Warm Gold
                    r = 1.00f * led_brightness;
                    g = 0.45f * led_brightness;
                    b = 0.02f * led_brightness;
                    break;
                case 2: // Hard Clip: Industrial Red / Magenta
                    r = 1.00f * led_brightness;
                    g = 0.05f * led_brightness;
                    b = 0.35f * led_brightness;
                    break;
            }
        }

        // Update all 4 RGB LEDs on Versio front panel
        for (int i = 0; i < 4; i++) {
            hw.leds[i].Set(r, g, b);
        }
        hw.UpdateLeds();

        System::Delay(4);
    }
}
