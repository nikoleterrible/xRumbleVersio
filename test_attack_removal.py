"""
test_attack_removal.py
Tests and compares rumble generation with:
- Steep 2-pole 95 Hz pre-filter
- 18ms transient attack blanking (no click in delay)
- 45ms ducking hold with zero leakage
"""

import math
from generate_audio_demos_v2 import make_pristine_kick, save_wav, BohmGrooveEngineDSP, SAMPLE_RATE, BPM

class PureSubRumbleEngine(BohmGrooveEngineDSP):
    def __init__(self, sample_rate=48000, bpm=132.0):
        super().__init__(sample_rate, bpm)
        # 2-pole Butterworth lowpass for pre-filter at 95 Hz
        fc = 95.0
        w = 2.0 * math.pi * fc / self.fs
        sin_w = math.sin(w)
        cos_w = math.cos(w)
        alpha = sin_w / (2.0 * 0.7071)
        a0 = 1.0 + alpha
        self.pre_b0 = ((1.0 - cos_w) * 0.5) / a0
        self.pre_b1 = (1.0 - cos_w) / a0
        self.pre_b2 = ((1.0 - cos_w) * 0.5) / a0
        self.pre_a1 = (-2.0 * cos_w) / a0
        self.pre_a2 = (1.0 - alpha) / a0
        self.pre_s1 = 0.0
        self.pre_s2 = 0.0

        # Transient Blanking counter (samples to mute send to delay)
        self.blank_counter = 0
        self.blank_total = int(self.fs * 0.022) # 22ms attack blanking window
        self.fade_start = int(self.fs * 0.014)  # 14ms silent, then 8ms smooth fade-in

        # Improved ducking hold (42 ms)
        self.duck_hold_samples = int(self.fs * 0.042)

    def trigger_duck(self):
        self.duck_env = 0.0
        self.duck_hold = self.duck_hold_samples
        self.blank_counter = self.blank_total

    def process(self, x):
        # 1. Faster kick detection
        if self.cooldown > 0:
            self.cooldown -= 1
        # Lower threshold for instant detection at 0.15 instead of 0.35
        if abs(x) > 0.15 and self.cooldown == 0:
            self.trigger_duck()
            self.cooldown = int(self.fs * 0.220)

        # 2. Transient Blanking: Strips the click/punch from entering the delay lines!
        send_gain = 1.0
        if self.blank_counter > 0:
            elapsed = self.blank_total - self.blank_counter
            if elapsed < self.fade_start:
                send_gain = 0.0 # 100% silence during beater click
            else:
                send_gain = (elapsed - self.fade_start) / (self.blank_total - self.fade_start)
            self.blank_counter -= 1

        raw_sub = x * send_gain

        # 3. Steep 2-pole 95 Hz Pre-Filter (removes any mid/high frequencies)
        lp_pre = self.pre_b0 * raw_sub + self.pre_s1
        self.pre_s1 = self.pre_b1 * raw_sub - self.pre_a1 * lp_pre + self.pre_s2
        self.pre_s2 = self.pre_b2 * raw_sub - self.pre_a2 * lp_pre
        sub_send = lp_pre

        # 4. Multi-tap groove delays
        beat_f = float(self.beat_samples)
        sig_l = 0.0
        sig_r = 0.0

        if self.fsu_roll:
            t32 = beat_f * 0.125
            t16 = beat_f * 0.250
            s1 = self.read_buffer(self.buf_l, t32)
            s2 = self.read_buffer(self.buf_r, t16)
            sig_l = s1 * 1.3 + s2 * 0.5
            sig_r = s2 * 1.3 + s1 * 0.5
        else:
            swing_off = self.swing * 0.18
            d2 = beat_f * 0.25 * (1.0 + swing_off)
            d3 = beat_f * 0.50
            d4 = beat_f * 0.75 * (1.0 + swing_off)
            taps = [d2, d3, d4]
            pans = [-0.75, 0.0, 0.75]

            if self.groove_shape < 0.30:
                weights = [0.10, 1.00, 0.35]
            elif self.groove_shape < 0.65:
                weights = [0.60, 0.85, 0.95]
            elif self.groove_shape < 0.85:
                weights = [0.85, 0.45, 1.00]
            else:
                weights = [0.75, 0.70, 0.90]

            for d, p, w in zip(taps, pans, weights):
                tap_s_l = self.read_buffer(self.buf_l, d)
                tap_s_r = self.read_buffer(self.buf_r, d)
                pan_eff = p * self.stereo_width
                gl = 0.5 * (1.0 - pan_eff)
                gr = 0.5 * (1.0 + pan_eff)
                sig_l += tap_s_l * w * gl
                sig_r += tap_s_r * w * gr

        # Feedback filtering
        self.fb_filt_l += 0.22 * (sig_l - self.fb_filt_l)
        self.fb_filt_r += 0.22 * (sig_r - self.fb_filt_r)
        fb_l = math.tanh(self.fb_filt_l * 1.2) * 0.85
        fb_r = math.tanh(self.fb_filt_r * 1.2) * 0.85

        fb_amount = 0.05 + self.swing * 0.45
        self.buf_l[self.write_idx] = sub_send + (fb_l * 0.7 + fb_r * 0.3) * fb_amount
        self.buf_r[self.write_idx] = sub_send + (fb_r * 0.7 + fb_l * 0.3) * fb_amount
        self.write_idx = (self.write_idx + 1) % self.buf_size

        # 5. Diffuser reverb
        c1 = self.c1_buf[self.c1_idx]
        self.c1_filt = c1 * 0.35 + self.c1_filt * 0.65
        self.c1_buf[self.c1_idx] = sig_l + self.c1_filt * 0.55
        self.c1_idx = (self.c1_idx + 1) % self.c1_len

        c2 = self.c2_buf[self.c2_idx]
        self.c2_filt = c2 * 0.35 + self.c2_filt * 0.65
        self.c2_buf[self.c2_idx] = sig_r + self.c2_filt * 0.55
        self.c2_idx = (self.c2_idx + 1) % self.c2_len

        rev_l = c1 * 0.50
        rev_r = c2 * 0.50

        rumble_l = sig_l * 0.65 + rev_l * 0.55
        rumble_r = sig_r * 0.65 + rev_r * 0.55

        # 6. Drive & Saturation
        xl = rumble_l * self.drive
        xr = rumble_r * self.drive
        sat_l = math.tanh(xl + 0.10 * xl * xl) * 0.70
        sat_r = math.tanh(xr + 0.10 * xr * xr) * 0.70

        # 7. Steep Lowpass (100 Hz)
        lp1_l = self.lp_b0_1 * sat_l + self.s1_1_l
        self.s1_1_l = self.lp_b1_1 * sat_l - self.lp_a1_1 * lp1_l + self.s2_1_l
        self.s2_1_l = self.lp_b2_1 * sat_l - self.lp_a2_1 * lp1_l
        lp2_l = self.lp_b0_2 * lp1_l + self.s1_2_l
        self.s1_2_l = self.lp_b1_2 * lp1_l - self.lp_a1_2 * lp2_l + self.s2_2_l
        self.s2_2_l = self.lp_b2_2 * lp1_l - self.lp_a2_2 * lp2_l

        lp1_r = self.lp_b0_1 * sat_r + self.s1_1_r
        self.s1_1_r = self.lp_b1_1 * sat_r - self.lp_a1_1 * lp1_r + self.s2_1_r
        self.s2_1_r = self.lp_b2_1 * sat_r - self.lp_a2_1 * lp1_r
        lp2_r = self.lp_b0_2 * lp1_r + self.s1_2_r
        self.s1_2_r = self.lp_b1_2 * lp1_r - self.lp_a1_2 * lp2_r + self.s2_2_r
        self.s2_2_r = self.lp_b2_2 * lp1_r - self.lp_a2_2 * lp2_r

        # 8. Ducking Envelope with ZERO leakage
        if self.duck_hold > 0:
            self.duck_hold -= 1
            self.duck_env = 0.0
        else:
            self.duck_env += self.rel_rate * (1.0 - self.duck_env)
            if self.duck_env > 1.0:
                self.duck_env = 1.0

        # Zero leakage: duck_env**2 so silence is 0.000
        shaped_duck = self.duck_env * self.duck_env
        duck_gain = shaped_duck # Exact 0.000f when ducking!

        ducked_l = lp2_l * duck_gain * 2.2
        ducked_r = lp2_r * duck_gain * 2.2

        # 9. Mid/Side Bass Management (<90 Hz mono)
        mid = (ducked_l + ducked_r) * 0.5
        side = (ducked_l - ducked_r) * 0.5

        filt_side = self.hp_b0 * side + self.side_s1
        self.side_s1 = self.hp_b1 * side - self.hp_a1 * filt_side + self.side_s2
        self.side_s2 = self.hp_b2 * side - self.hp_a2 * filt_side

        scaled_side = filt_side * self.stereo_width

        final_l = mid + scaled_side
        final_r = mid - scaled_side

        out_l = math.tanh(final_l * 1.35) * 0.94
        out_r = math.tanh(final_r * 1.35) * 0.94

        return out_l, out_r

if __name__ == "__main__":
    print("Testing Pure Sub Rumble without kick attack...")
    kick = make_pristine_kick(0.42)
    samples_per_beat = int(SAMPLE_RATE * (60.0 / BPM))
    total_beats = 16
    total_len = samples_per_beat * total_beats

    dry_track = [0.0] * total_len
    for b in range(total_beats):
        st = b * samples_per_beat
        for i, s in enumerate(kick):
            if st + i < total_len:
                dry_track[st + i] += s

    # Generate Pure Sub Rumble (Zero attack leak)
    dsp = PureSubRumbleEngine(SAMPLE_RATE, BPM)
    dsp.groove_shape = 0.45
    dsp.stereo_width = 0.85
    wet = [dsp.process(s) for s in dry_track]
    save_wav("d:/xRumbleVersio/test_pure_sub_no_attack.wav", wet)
    print("Done!")
