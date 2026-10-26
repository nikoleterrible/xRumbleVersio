"""
test_bohm_taps_dsp.py
Accurately implements the user's requested Bohm Groove control scheme:
- Blend (Knob 0): TAP 1 Level (16th #2, panned Left)
- Tone (Knob 1): TAP 2 Level (16th #3 / Offbeat, panned Center)
- Rain (Knob 2): TAP 3 Level (16th #4, panned Right)
- Speed (Knob 3): FILTER FREQUENCY (Cutoff 45Hz - 500Hz)
- Index (Knob 4): FILTER RESONANCE (Resonance Q 0.7 - 4.0)
- Size (Knob 5): REVERB SIZE & DECAY (dry punchy to cavernous space)
- Decay (Knob 6): DRIVE / FX (Clean to Heavy Saturated)
- Switch 1 (ABC): GRID (16th Straight / Triplet 12-T / Dotted)
- Switch 2 (XYZ): SATURATION (Clean / Tape / Hard Clip)
- FSU Button & FSU CV: 32nd-note Roll Burst
- 100% WET True Stereo Output with <90Hz Mono Sub Bass Management
"""

import math
import struct
import wave

SAMPLE_RATE = 48000
BPM = 132.0

def make_pristine_kick(duration_sec=0.45):
    num_samples = int(SAMPLE_RATE * duration_sec)
    samples = [0.0] * num_samples
    phase = 0.0
    for i in range(num_samples):
        t = i / float(SAMPLE_RATE)
        pitch = 48.0 + (145.0 - 48.0) * math.exp(-t * 38.0)
        phase += 2.0 * math.pi * pitch / SAMPLE_RATE
        body = math.sin(phase)
        click_phase = 2.0 * math.pi * 900.0 * t
        click = math.sin(click_phase) * math.exp(-t * 280.0) * 0.22
        attack = 1.0 - math.exp(-t * 1500.0)
        decay = math.exp(-t * 8.5)
        end_fade = 1.0
        fade_start = 0.35
        if t > fade_start:
            end_fade = max(0.0, 1.0 - (t - fade_start) / (duration_sec - fade_start))
        samples[i] = (body + click) * attack * decay * end_fade * 0.85
    return samples

def save_wav(filename, samples):
    with wave.open(filename, 'wb') as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        raw_bytes = bytearray()
        for s in samples:
            val_l = max(-1.0, min(1.0, s[0]))
            val_r = max(-1.0, min(1.0, s[1]))
            int_l = int(val_l * 32767.0)
            int_r = int(val_r * 32767.0)
            raw_bytes.extend(struct.pack('<hh', int_l, int_r))
        wf.writeframes(raw_bytes)
    print(f"  -> Generated: {filename}")

class BohmGrooveFullDSP:
    def __init__(self, sample_rate=48000, bpm=132.0):
        self.fs = sample_rate
        self.bpm = bpm
        self.beat_samples = int(self.fs * (60.0 / bpm))

        # Controls requested by user:
        self.tap1_level = 0.0 # Knob 0: Blend (Tap 1)
        self.tap2_level = 1.0 # Knob 1: Tone (Tap 2 - Offbeat)
        self.tap3_level = 0.5 # Knob 2: Rain (Tap 3)
        self.filter_freq_knob = 0.40 # Knob 3: Speed (Filter Cutoff 45Hz - 500Hz)
        self.filter_res_knob = 0.25  # Knob 4: Index (Resonance Q 0.7 - 4.0)
        self.reverb_size_knob = 0.50 # Knob 5: Size (Reverb decay)
        self.drive_knob = 0.50       # Knob 6: Decay (Drive / FX)

        # Switches
        self.grid_mode = 0 # Switch 1: 0=16th Straight, 1=12-T Triplet, 2=Dotted
        self.sat_mode = 1  # Switch 2: 0=Clean Sub Pur, 1=Warm Tape, 2=Hard Clip

        # Performance FSU
        self.fsu_active = False
        self.fsu_phase = 0.0

        # Sub Reverb Comb Filters
        self.c1_len = int(self.fs / 48.0) # ~1000
        self.c2_len = int(self.fs / 54.0) # ~888
        self.c3_len = int(self.fs / 64.0) # ~750
        self.c1_buf = [0.0] * self.c1_len
        self.c2_buf = [0.0] * self.c2_len
        self.c3_buf = [0.0] * self.c3_len
        self.c1_idx = 0; self.c2_idx = 0; self.c3_idx = 0
        self.c1_filt = 0.0; self.c2_filt = 0.0; self.c3_filt = 0.0

        # Biquad Resonant Lowpass Filter (State variables for L & R)
        self.lpf_s1_l = 0.0; self.lpf_s2_l = 0.0
        self.lpf_s1_r = 0.0; self.lpf_s2_r = 0.0
        self.cutoff_hz = 120.0
        self.resonance_q = 1.0
        self.update_biquad_lpf()

        # Side Highpass at 90 Hz for Mono Sub
        self.setup_side_hp(90.0)

        # Beat phase tracking
        self.phase_samples = 0
        self.cooldown = 0

        # Ghost tap phase generators
        self.t1_phase = 1.0
        self.t2_phase = 1.0
        self.t3_phase = 1.0
        self.t1_len = int(self.fs * 0.110) # 110ms tap duration
        self.t2_len = int(self.fs * 0.125)
        self.t3_len = int(self.fs * 0.110)

    def setup_side_hp(self, fc):
        w = 2.0 * math.pi * fc / self.fs
        sin_w = math.sin(w)
        cos_w = math.cos(w)
        alpha = sin_w / (2.0 * 0.7071)
        a0 = 1.0 + alpha
        self.hp_b0 = ((1.0 + cos_w) * 0.5) / a0
        self.hp_b1 = -(1.0 + cos_w) / a0
        self.hp_b2 = ((1.0 + cos_w) * 0.5) / a0
        self.hp_a1 = (-2.0 * cos_w) / a0
        self.hp_a2 = (1.0 - alpha) / a0
        self.side_s1 = 0.0; self.side_s2 = 0.0

    def update_biquad_lpf(self):
        fc = 45.0 * math.pow(500.0 / 45.0, self.filter_freq_knob)
        q = 0.707 + self.filter_res_knob * 3.3
        self.cutoff_hz = fc
        self.resonance_q = q

        w = 2.0 * math.pi * fc / self.fs
        sin_w = math.sin(w)
        cos_w = math.cos(w)
        alpha = sin_w / (2.0 * q)
        a0 = 1.0 + alpha
        self.lp_b0 = ((1.0 - cos_w) * 0.5) / a0
        self.lp_b1 = (1.0 - cos_w) / a0
        self.lp_b2 = ((1.0 - cos_w) * 0.5) / a0
        self.lp_a1 = (-2.0 * cos_w) / a0
        self.lp_a2 = (1.0 - alpha) / a0

    def trigger_tap_envelope(self, phase, step_target):
        # Triggers a smooth bell envelope for a tap at step_target
        # Window width = 0.28 beat (~125ms)
        diff = phase - step_target
        if 0.0 <= diff < 0.26:
            p = diff / 0.26
            # Raised sine bell envelope: starts at 0, peaks at 0.5, returns to 0
            # Multiplied by pure sine wave at 48 Hz
            env = math.sin(math.pi * p)
            # Smooth exponential swell:
            swell = env * env
            sine = math.sin(2.0 * math.pi * 50.0 * (diff * self.beat_samples / self.fs))
            return swell * sine
        return 0.0

    def process(self, x):
        # 1. Automatic kick onset tracking from audio on IN L
        if self.cooldown > 0:
            self.cooldown -= 1
        if abs(x) > 0.15 and self.cooldown == 0:
            self.phase_samples = 0
            self.cooldown = int(self.fs * 0.220)

        phase = float(self.phase_samples) / float(self.beat_samples)
        self.phase_samples = (self.phase_samples + 1) % self.beat_samples

        # 2. Tap Step Timing: dependent on Switch 1 (Grid Mode)
        if self.grid_mode == 1:
            # Triplet 12-T Grid
            step1 = 1.0 / 6.0 # 0.166
            step2 = 2.0 / 6.0 # 0.333
            step3 = 4.0 / 6.0 # 0.666
        elif self.grid_mode == 2:
            # Dotted Grid
            step1 = 0.1875
            step2 = 0.3750
            step3 = 0.6250
        else:
            # 16th Straight Grid
            step1 = 0.250 # 16th note #2
            step2 = 0.500 # 16th note #3 (Offbeat)
            step3 = 0.750 # 16th note #4

        # 3. Generate pure sub ghost pulses (ZERO clicks, pure sine weight!)
        tap1_wave = self.trigger_tap_envelope(phase, step1) * self.tap1_level
        tap2_wave = self.trigger_tap_envelope(phase, step2) * self.tap2_level
        tap3_wave = self.trigger_tap_envelope(phase, step3) * self.tap3_level

        # Panning in Stereo:
        # Tap 1: Panned Left (-0.75)
        # Tap 2: Panned Center (0.0 - Offbeat foundation)
        # Tap 3: Panned Right (+0.75)
        t1_l = tap1_wave * 0.85
        t1_r = tap1_wave * 0.15

        t2_l = tap2_wave * 0.50
        t2_r = tap2_wave * 0.50

        t3_l = tap3_wave * 0.15
        t3_r = tap3_wave * 0.85

        taps_l = t1_l + t2_l + t3_l
        taps_r = t1_r + t2_r + t3_r

        # 4. Reverb Size & Decay (Knob 5: Size)
        reverb_fb = 0.15 + self.reverb_size_knob * 0.75
        c1 = self.c1_buf[self.c1_idx]
        self.c1_filt += 0.18 * (c1 - self.c1_filt)
        self.c1_buf[self.c1_idx] = taps_l + self.c1_filt * reverb_fb
        self.c1_idx = (self.c1_idx + 1) % self.c1_len

        c2 = self.c2_buf[self.c2_idx]
        self.c2_filt += 0.18 * (c2 - self.c2_filt)
        self.c2_buf[self.c2_idx] = taps_r + self.c2_filt * reverb_fb
        self.c2_idx = (self.c2_idx + 1) % self.c2_len

        rev_l = c1 * (self.reverb_size_knob * 0.60)
        rev_r = c2 * (self.reverb_size_knob * 0.60)

        sig_l = taps_l + rev_l
        sig_r = taps_r + rev_r

        # 5. FSU Performance Roll (FSU Button / FSU CV)
        if self.fsu_active:
            self.fsu_phase += 8.0 / float(self.beat_samples)
            if self.fsu_phase >= 1.0: self.fsu_phase -= 1.0
            roll_amp = 0.5 * (1.0 - math.cos(2.0 * math.pi * self.fsu_phase)) * 1.5
            sig_l *= roll_amp
            sig_r *= roll_amp

        # 6. Drive & Saturation (Knob 6: Decay/Dense) + Switch 2 (Sat Mode)
        drive_mult = 1.0 + self.drive_knob * 7.0 # 1.0x to 8.0x
        xl = sig_l * drive_mult
        xr = sig_r * drive_mult

        if self.sat_mode == 0:
            # Clean Sub Pur
            sat_l = math.tanh(xl) * (1.0 / math.sqrt(drive_mult))
            sat_r = math.tanh(xr) * (1.0 / math.sqrt(drive_mult))
        elif self.sat_mode == 1:
            # Warm Tape
            sat_l = math.tanh(xl + 0.12 * xl * xl) * 0.80
            sat_r = math.tanh(xr + 0.12 * xr * xr) * 0.80
        else:
            # Hard Clip / Wavefold
            def fold(v):
                if v > 1.2: return 2.4 - v
                elif v < -1.2: return -2.4 - v
                return max(-1.0, min(1.0, v))
            sat_l = fold(xl) * 0.90
            sat_r = fold(xr) * 0.90

        # 7. Resonant Low-Pass Filter (Knob 3: Speed = Cutoff, Knob 4: Index = Resonance)
        self.update_biquad_lpf()

        lp_l = self.lp_b0 * sat_l + self.lpf_s1_l
        self.lpf_s1_l = self.lp_b1 * sat_l - self.lp_a1 * lp_l + self.lpf_s2_l
        self.lpf_s2_l = self.lp_b2 * sat_l - self.lp_a2 * lp_l

        lp_r = self.lp_b0 * sat_r + self.lpf_s1_r
        self.lpf_s1_r = self.lp_b1 * sat_r - self.lp_a1 * lp_r + self.lpf_s2_r
        self.lpf_s2_r = self.lp_b2 * sat_r - self.lp_a2 * lp_r

        # 8. Clean Ducking on Kick Impact (0.00 to 0.10: 100% Silence for the kick)
        duck = 1.0
        if phase < 0.10:
            duck = 0.0
        elif phase < 0.22:
            p = (phase - 0.10) / 0.12
            duck = p * p

        # 9. Anticipatory Tuck-In on last 6% of beat (ZERO BOUNDARY CLICK!)
        if phase > 0.94:
            p = (1.0 - phase) / 0.06
            duck *= p * p

        ducked_l = lp_l * duck * 2.5
        ducked_r = lp_r * duck * 2.5

        # 10. Mid/Side Bass Management (<90Hz mono)
        mid = (ducked_l + ducked_r) * 0.5
        side = (ducked_l - ducked_r) * 0.5

        filt_side = self.hp_b0 * side + self.side_s1
        self.side_s1 = self.hp_b1 * side - self.hp_a1 * filt_side + self.side_s2
        self.side_s2 = self.hp_b2 * side - self.hp_a2 * filt_side

        out_l = math.tanh(mid + filt_side) * 0.94
        out_r = math.tanh(mid - filt_side) * 0.94

        return out_l, out_r

if __name__ == "__main__":
    print("Testing Bohm Taps & Filter Resonance DSP...")
    kick = make_pristine_kick(0.42)
    beat_len = int(SAMPLE_RATE * (60.0 / BPM))
    total_beats = 16
    total_len = beat_len * total_beats

    dry = [0.0] * total_len
    for b in range(total_beats):
        st = b * beat_len
        for i, s in enumerate(kick):
            if st + i < total_len:
                dry[st + i] += s

    # Demo 1: Offbeat Only (Blend=0.0, Tone=1.0, Rain=0.0) -> Pure Berlin Offbeat
    dsp1 = BohmGrooveFullDSP(SAMPLE_RATE, BPM)
    dsp1.tap1_level = 0.0
    dsp1.tap2_level = 1.0
    dsp1.tap3_level = 0.0
    dsp1.filter_freq_knob = 0.35 # 105 Hz
    dsp1.filter_res_knob = 0.20
    dsp1.drive_knob = 0.40
    wet1 = [dsp1.process(s) for s in dry]
    save_wav("d:/xRumbleVersio/demo_v2_1_pure_rumble_offbeat_stereo.wav", wet1)

    # Demo 2: Rolling 16th (Blend=0.75, Tone=1.0, Rain=0.75) -> All 3 Taps dancing in Stereo!
    dsp2 = BohmGrooveFullDSP(SAMPLE_RATE, BPM)
    dsp2.tap1_level = 0.75
    dsp2.tap2_level = 1.00
    dsp2.tap3_level = 0.75
    dsp2.filter_freq_knob = 0.45 # 140 Hz
    dsp2.filter_res_knob = 0.35
    dsp2.drive_knob = 0.50
    wet2 = [dsp2.process(s) for s in dry]
    save_wav("d:/xRumbleVersio/demo_v2_2_pure_rumble_train_stereo.wav", wet2)

    # Demo 3: Acid Resonant Filter Sweep (Resonance Q boosted, Cutoff opened to 240 Hz)
    dsp3 = BohmGrooveFullDSP(SAMPLE_RATE, BPM)
    dsp3.tap1_level = 0.60
    dsp3.tap2_level = 1.00
    dsp3.tap3_level = 0.60
    dsp3.filter_freq_knob = 0.70 # 240 Hz
    dsp3.filter_res_knob = 0.85  # Heavy resonance!
    dsp3.drive_knob = 0.75       # Heavy drive
    dsp3.sat_mode = 2            # Hard clip
    wet3 = [dsp3.process(s) for s in dry]
    save_wav("d:/xRumbleVersio/demo_v2_3_pure_rumble_triplet_stereo.wav", wet3)

    # Demo 4: Parallel Full Mix with Kick + FSU Roll Burst on beats 10-11
    dsp4 = BohmGrooveFullDSP(SAMPLE_RATE, BPM)
    dsp4.tap1_level = 0.60
    dsp4.tap2_level = 1.00
    dsp4.tap3_level = 0.60
    dsp4.filter_freq_knob = 0.40
    dsp4.drive_knob = 0.45
    full_mix = []
    for idx, s in enumerate(dry):
        beat_idx = idx // beat_len
        dsp4.fsu_active = (beat_idx in [10, 11])
        wl, wr = dsp4.process(s)
        mix_l = math.tanh(s * 0.85 + wl * 0.90) * 0.95
        mix_r = math.tanh(s * 0.85 + wr * 0.90) * 0.95
        full_mix.append((mix_l, mix_r))
    save_wav("d:/xRumbleVersio/demo_v2_4_full_mix_with_fsu_roll.wav", full_mix)

    print("All Bohm Taps demos generated successfully!")
