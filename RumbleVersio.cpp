#include "daisy_versio.h"
#include "Dsp/BohmGrooveEngine.h"

using namespace daisy;
using namespace rumble;

// Hardware interface
static DaisyVersio hw;

// DSP Engine
static BohmGrooveEngine groove_engine;

// LED and UI tracking
static float led_levels[4] = {0.0f, 0.0f, 0.0f, 0.0f};
static int   current_sat_mode = 1;
static bool  fsu_button_pressed = false;
static float fsu_pulse_phase = 0.0f;

// External Clock Tracking on FSU Jack (Gate In)
static uint32_t last_clock_samples = 0;
static uint32_t clock_interval_samples = 21818; // Default: 132 BPM at 48kHz
static bool     clock_has_external = false;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    // Read and debounce hardware controls
    hw.ProcessAnalogControls();
    hw.tap.Debounce();

    // 1. PHYSICAL KNOB MAPPING (Noise Engineering Versio Hardware Layout):
    // Top Row:
    // Blend (Top Left, Knob 1)   : KNOB_0 -> Tap 1 Level (16e #2, pan Gauche)
    float k_blend = hw.GetKnobValue(DaisyVersio::KNOB_0);
    // Tone  (Top Center, Knob 3) : KNOB_2 -> Downbeat Level (Temps 1 Sub, pan Centre)
    float k_tone  = hw.GetKnobValue(DaisyVersio::KNOB_2);
    // Regen (Top Right, Knob 2)  : KNOB_1 -> Tap 2 / Offbeat Level (16e #3, pan Droite)
    float k_regen = hw.GetKnobValue(DaisyVersio::KNOB_1);

    // Middle Row:
    // Speed (Mid Left, Knob 6)   : KNOB_5 -> SVF Filter Cutoff (30 Hz à 3200 Hz!)
    float k_speed = hw.GetKnobValue(DaisyVersio::KNOB_5);
    // Index (Mid Center, Knob 5) : KNOB_4 -> SVF Resonance Q (0.707 à 6.5!)
    float k_index = hw.GetKnobValue(DaisyVersio::KNOB_4);
    // Size  (Mid Right, Knob 4)  : KNOB_3 -> Reverb Space / Diffusion (0 = dry taps, 1 = dense warehouse)
    float k_size  = hw.GetKnobValue(DaisyVersio::KNOB_3);

    // Lower Right:
    // Dense (Lower Right, Knob 7): KNOB_6 -> Saturation Drive (1.0x à 4.2x)
    float k_dense = hw.GetKnobValue(DaisyVersio::KNOB_6);

    // 2. SWITCH 0 (Top Toggle: LIM / DST / SHM) - Saturation Character Mode
    // UP (POS_UP = 1)         -> LIM: Sub Pur (Clean round soft-knee, pure fundamental)
    // CENTER (POS_CENTER = 0) -> DST: Warm Tape (Analog console warmth & 2nd/3rd harmonics)
    // DOWN (POS_DOWN = 2)     -> SHM: Fat Crunch (Diode bite & asymmetric grit)
    int sw_sat_raw = hw.sw[DaisyVersio::SW_0].Read();
    int sat_mode = 0;
    if (sw_sat_raw == Switch3::POS_UP) {
        sat_mode = 0; // LIM: Sub Pur
    } else if (sw_sat_raw == Switch3::POS_CENTER) {
        sat_mode = 1; // DST: Warm Tape
    } else {
        sat_mode = 2; // SHM: Fat Crunch
    }
    current_sat_mode = sat_mode;

    // 3. SWITCH 1 (Bottom Toggle: BND / LRP / JMP) - Micro-timing Offset (Ajustement Décalage)
    // UP (POS_UP = 1)         -> BND: Ahead / Rush (-12 ms, groove tiré en avant)
    // CENTER (POS_CENTER = 0) -> LRP: On-Beat (0 ms, calage neutre exact)
    // DOWN (POS_DOWN = 2)     -> JMP: Laid-Back Drag (+18 ms, retard chaloupé / swing techno)
    int sw_offset_raw = hw.sw[DaisyVersio::SW_1].Read();
    int timing_offset_mode = 1;
    if (sw_offset_raw == Switch3::POS_UP) {
        timing_offset_mode = 0; // Ahead / Rush (-12 ms)
    } else if (sw_offset_raw == Switch3::POS_CENTER) {
        timing_offset_mode = 1; // On-Beat (0 ms)
    } else {
        timing_offset_mode = 2; // Laid-Back Drag (+18 ms)
    }

    // 4. FSU BUTTON (Tap momentary pushbutton): Freeze Wash Riser Effect
    fsu_button_pressed = hw.tap.Pressed();

    // 5. Update DSP parameters
    groove_engine.SetParameters(k_blend, k_tone, k_regen,
                                k_speed, k_index, k_size, k_dense,
                                timing_offset_mode, sat_mode,
                                clock_interval_samples, fsu_button_pressed);

    // 6. Audio Block Processing with Gate Clock on FSU Jack
    for (size_t i = 0; i < size; i++)
    {
        // Check for incoming Clock pulse on FSU Jack (Gate In)
        if (hw.gate.Trig())
        {
            if (last_clock_samples > 2000 && last_clock_samples < 160000) // ~18 BPM to 1440 BPM
            {
                clock_interval_samples = last_clock_samples;
                clock_has_external = true;
            }
            last_clock_samples = 0;
            groove_engine.TriggerSync(); // Hard reset phase to 0 on clock pulse!
        }
        else
        {
            if (last_clock_samples < 200000) {
                last_clock_samples++;
            }
        }

        float in_l = in[0][i];
        float in_r = in[1][i];
        float in_mono = (in_l + in_r) * 0.5f;

        // Generate 100% Wet Stereo Sub Rumble
        float wet_l, wet_r;
        groove_engine.Process(in_mono, wet_l, wet_r);

        // True Stereo Outputs (100% Wet Pure Rumble)
        out[0][i] = wet_l; // OUT L: 100% Pure Stereo Wet Rumble Left
        out[1][i] = wet_r; // OUT R: 100% Pure Stereo Wet Rumble Right
    }

    // 7. Update LED visualizer:
    float phase = groove_engine.GetCurrentPhase();
    float t_down = groove_engine.GetDownbeatLevel();
    float t_tap1 = groove_engine.GetTap1Level();
    float t_tap2 = groove_engine.GetTap2Level();

    led_levels[0] += 0.35f * (((phase < 0.20f) ? (0.2f + 0.8f * t_down) : 0.05f) - led_levels[0]);
    led_levels[1] += 0.35f * (((phase >= 0.22f && phase < 0.44f) ? (0.2f + 0.8f * t_tap1) : 0.05f) - led_levels[1]);
    led_levels[2] += 0.35f * (((phase >= 0.46f && phase < 0.70f) ? (0.2f + 0.8f * t_tap2) : 0.05f) - led_levels[2]);
    led_levels[3] += 0.35f * (((phase >= 0.72f && phase < 0.95f) ? (0.2f + 0.8f * t_tap2) : 0.05f) - led_levels[3]);
}

int main(void)
{
    // Initialize Versio hardware with high-performance CPU clock (480 MHz)
    hw.Init(true);

    // CRITICAL: Start ADC DMA conversion for all 7 knobs & CV inputs!
    hw.StartAdc();

    float sample_rate = hw.AudioSampleRate();

    // Initialize DSP block
    groove_engine.Init(sample_rate);

    // Block size: 16 samples for ultra-low latency (< 0.35 ms)
    hw.SetAudioBlockSize(16);

    // Start audio stream
    hw.StartAudio(AudioCallback);

    while (1)
    {
        fsu_pulse_phase += 0.04f;
        if (fsu_pulse_phase > 6.2831853f) fsu_pulse_phase -= 6.2831853f;

        // 4 RGB LEDs reflect the 4 steps of the groove pattern:
        for (int i = 0; i < 4; i++)
        {
            float brightness = led_levels[i];
            float r = 0.0f, g = 0.0f, b = 0.0f;

            if (groove_engine.IsFsuActive()) {
                // FSU Freeze Wash Active: Pulsing ethereal violet / magenta wash
                float pulse = 0.5f + 0.5f * sinf(fsu_pulse_phase + i * 1.57f);
                r = 0.85f * pulse;
                g = 0.10f * pulse;
                b = 1.00f * pulse;
            } else {
                switch (current_sat_mode) {
                    case 0: // Sub Pur (LIM): Electric Cyan / Blue
                        r = 0.05f * brightness;
                        g = 0.65f * brightness;
                        b = 1.00f * brightness;
                        break;
                    case 1: // Warm Tape (DST): Warm Amber / Gold
                        r = 1.00f * brightness;
                        g = 0.45f * brightness;
                        b = 0.02f * brightness;
                        break;
                    case 2: // Fat Crunch (SHM): Deep Red / Coral
                        r = 1.00f * brightness;
                        g = 0.15f * brightness;
                        b = 0.10f * brightness;
                        break;
                }
            }

            hw.leds[i].Set(r, g, b);
        }

        hw.UpdateLeds();
    }
}
