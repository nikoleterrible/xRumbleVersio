#pragma once
#include <cmath>
#include <algorithm>

namespace rumble {

enum class SaturationMode {
    kSubPur = 0,   // Clean, soft-knee transparent compression
    kWarmTape = 1, // Asymmetric saturation with 2nd & 3rd harmonics
    kHardClip = 2  // Hard diode clip & wavefold for industrial bite
};

/**
 * SaturationStage:
 * Generates harmonics to make the sub rumble cut through small speakers and PA systems.
 */
class SaturationStage {
public:
    SaturationStage() : drive_(1.0f), mode_(SaturationMode::kWarmTape) {}

    void Init() {
        drive_ = 1.0f;
        mode_ = SaturationMode::kWarmTape;
    }

    /**
     * @param speed_knob: 0.0 (gentle 1.0x drive) to 1.0 (heavy 8.0x drive)
     * @param switch_mode: 0, 1, or 2 from hardware switch 1
     */
    void SetParameters(float speed_knob, int switch_mode) {
        // Drive ranges from 1.0 to 7.0
        drive_ = 1.0f + speed_knob * 6.0f;

        switch (switch_mode) {
            case 0:
                mode_ = SaturationMode::kSubPur;
                break;
            case 1:
                mode_ = SaturationMode::kWarmTape;
                break;
            case 2:
            default:
                mode_ = SaturationMode::kHardClip;
                break;
        }
    }

    float Process(float in) {
        float x = in * drive_;

        switch (mode_) {
            case SaturationMode::kSubPur: {
                // Soft limiter curve (tanh with compensation)
                float norm = 1.0f / sqrtf(drive_);
                return tanhf(x) * norm;
            }

            case SaturationMode::kWarmTape: {
                // Asymmetric saturation: introduces 2nd harmonic (even) + 3rd harmonic (odd)
                // f(x) = tanh(x + 0.15*x^2)
                float asym = x + 0.15f * (x * x);
                float sat = tanhf(asym);
                // Level compensation
                float norm = 1.0f / (0.8f + 0.4f * sqrtf(drive_));
                return sat * norm;
            }

            case SaturationMode::kHardClip: {
                // Wavefold / Hard Diode clipping
                float folded = x;
                if (folded > 1.2f) {
                    folded = 2.4f - folded; // Fold back
                } else if (folded < -1.2f) {
                    folded = -2.4f - folded;
                }
                // Hard clamp
                if (folded > 1.0f) folded = 1.0f;
                if (folded < -1.0f) folded = -1.0f;
                float norm = 1.0f / (1.0f + 0.3f * drive_);
                return folded * norm;
            }
        }

        return in;
    }

    SaturationMode GetMode() const { return mode_; }
    float GetDrive() const { return drive_; }

private:
    float drive_;
    SaturationMode mode_;
};

} // namespace rumble
