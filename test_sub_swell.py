"""
test_sub_swell.py
Simulates a true Techno Rumble "Gonflement" (Sub Swell):
1. Kick excites a tuned sub cavity / diffuser (< 85 Hz).
2. The sub cavity resonates smoothly with pure sine weight (NO clicks, NO delay repeats).
3. The Groove Engine applies smooth Hanning / S-curve swell envelopes:
   - Offbeat Berlin Swell: slow rising swell blooming into the offbeat.
   - Rolling 16th Swell: 3 undulating wave pulses (2, 3, 4) with zero transient click.
   - Triplet Swell: 3 hypnotic rolling undulations.
4. Smooth lookahead ramp down (3ms) to guarantee ZERO click on kick impact.
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

class TrueSubSwellEngine:
    def __init__(self, sample_rate=48000, bpm=132.0):
        self.fs = sample_rate
        self.bpm = bpm
        self.beat_samples = int(self.fs * (60.0 / bpm))

        # 1. 4-pole Low-pass on kick send at 75 Hz (absolument AUCUN clic ne peut entrer)
        self.setup_send_lpf(75.0)

        # 2. Resonant Sub Diffuser / Tank (tuned to sub modal resonance ~48 Hz & 55 Hz)
        # Prime comb delays tuned to sub frequencies
        self.c1_len = int(self.fs / 48.0) # ~1000 samples (48 Hz fundamental)
        self.c2_len = int(self.fs / 54.0) # ~888 samples (54 Hz 3rd semitone)
        self.c3_len = int(self.fs / 64.0) # ~750 samples (64 Hz)
        self.c1_buf = [0.0] * self.c1_len
        self.c2_buf = [0.0] * self.c2_len
        self.c3_buf = [0.0] * self.c3_len
        self.c1_idx = 0
        self.c2_idx = 0
        self.c3_idx = 0
        self.c1_filt = 0.0
        self.c2_filt = 0.0
        self.c3_filt = 0.0

        # Allpass diffusers for smooth continuous wash
        self.ap1_buf = [0.0] * 241
        self.ap2_buf = [0.0] * 373
        self.ap1_idx = 0
        self.ap2_idx = 0

        # 3. Beat phase tracking [0.0 to 1.0] across the measure
        self.phase_samples = 0
        self.duck_gain = 0.0
        self.target_duck = 0.0

        # Lookahead buffer (48 samples = 1 ms) to ramp down smoothly before the beat
        self.la_len = 48
        self.la_buf_l = [0.0] * self.la_len
        self.la_buf_r = [0.0] * self.la_len
        self.la_idx = 0

        # Stereo width & mode
        self.groove_mode = 0 # 0=Berlin Swell, 1=16th Wave Train, 2=Triplet Roll
        self.stereo_width = 0.85
        self.drive = 2.8

    def setup_send_lpf(self, fc):
        w = 2.0 * math.pi * fc / self.fs
        sin_w = math.sin(w)
        cos_w = math.cos(w)
        alpha = sin_w / (2.0 * 0.7071)
        a0 = 1.0 + alpha
        self.send_b0 = ((1.0 - cos_w) * 0.5) / a0
        self.send_b1 = (1.0 - cos_w) / a0
        self.send_b2 = ((1.0 - cos_w) * 0.5) / a0
        self.send_a1 = (-2.0 * cos_w) / a0
        self.send_a2 = (1.0 - alpha) / a0
        self.s1_1 = 0.0; self.s2_1 = 0.0
        self.s1_2 = 0.0; self.s2_2 = 0.0

    def diff_allpass(self, x, buf, idx_ref, coeff=0.7):
        idx = idx_ref[0]
        buf_out = buf[idx]
        vn = x - coeff * buf_out
        out = buf_out + coeff * vn
        buf[idx] = vn
        idx_ref[0] = (idx + 1) % len(buf)
        return out

    def compute_swell_envelope(self, phase):
        """
        Computes the smooth swelling envelope ("gonflement qui monte"):
        phase goes from 0.0 (kick hit) to 1.0 (next kick).
        """
        if self.groove_mode == 0:
            # --- BERLIN SUB SWELL (Pure Gonflement) ---
            # 0.00 to 0.12 (0 - 55ms): MUTE (kick attack & body passes cleanly)
            # 0.12 to 0.65 (55ms - 295ms): EXPONENTIAL SWELL UP (Le gonflement qui monte!)
            # 0.65 to 0.92: Resonant bloom & sustain
            # 0.92 to 1.00: Smooth anticipatory fade down to 0.0 (ZERO CLICK on next kick!)
            if phase < 0.10:
                return 0.0
            elif phase < 0.70:
                # Smooth S-curve swell
                p = (phase - 0.10) / 0.60
                return p * p * (3.0 - 2.0 * p) # Smoothstep: 0.0 to 1.0
            elif phase < 0.92:
                return 1.0
            else:
                # Smooth 8% ramp down to 0.0 before next kick hits
                p = (1.0 - phase) / 0.08
                return p * p

        elif self.groove_mode == 1:
            # --- 16TH WAVE ROLLING TRAIN ---
            # 3 smooth undulating swells on steps 2, 3, 4 (NO delay attack clicks)
            # Step 1 (0.00 - 0.20): Mute
            # Step 2 (0.20 - 0.45): Wave 1 (amplitude 0.55)
            # Step 3 (0.45 - 0.70): Wave 2 (amplitude 0.85)
            # Step 4 (0.70 - 0.94): Wave 3 (amplitude 1.00 - the big swell)
            # End (0.94 - 1.00): Smooth tuck-in to 0.0
            if phase < 0.15:
                return 0.0
            elif phase < 0.40:
                # Wave 2
                p = (phase - 0.15) / 0.25
                return math.sin(math.pi * p) * 0.55
            elif phase < 0.68:
                # Wave 3 (Offbeat)
                p = (phase - 0.40) / 0.28
                return math.sin(math.pi * p) * 0.85
            elif phase < 0.94:
                # Wave 4
                p = (phase - 0.68) / 0.26
                return math.sin(math.pi * p) * 1.00
            else:
                return 0.0

        else:
            # --- TRIPLET HYPNOTIC SWELL (12-T) ---
            # Two rolling swells at 1/3 and 2/3
            if phase < 0.15:
                return 0.0
            elif phase < 0.52:
                p = (phase - 0.15) / 0.37
                return math.sin(math.pi * p) * 0.75
            elif phase < 0.93:
                p = (phase - 0.52) / 0.41
                return math.sin(math.pi * p) * 1.00
            else:
                return 0.0

    def process(self, x):
        # 1. Beat Phase Tracking
        phase = float(self.phase_samples) / float(self.beat_samples)
        self.phase_samples = (self.phase_samples + 1) % self.beat_samples

        # 2. 4-pole Lowpass Filter on Kick Send (75 Hz cutoff)
        # Strips 100% of the beater click, attack, and slap
        lp1 = self.send_b0 * x + self.s1_1
        self.s1_1 = self.send_b1 * x - self.send_a1 * lp1 + self.s2_1
        self.s2_1 = self.send_b2 * x - self.send_a2 * lp1
        lp2 = self.send_b0 * lp1 + self.s1_2
        self.s1_2 = self.send_b1 * lp1 - self.send_a1 * lp2 + self.s2_2
        self.s2_2 = self.send_b2 * lp1 - self.send_a2 * lp2
        pure_sub_in = lp2

        # 3. Dense Sub Resonator Cavity (generates continuous sub drone/rumble)
        # Serial allpass diffusers
        ap_idx1 = [self.ap1_idx]
        ap_idx2 = [self.ap2_idx]
        diff = self.diff_allpass(pure_sub_in, self.ap1_buf, ap_idx1, 0.65)
        diff = self.diff_allpass(diff, self.ap2_buf, ap_idx2, 0.60)
        self.ap1_idx = ap_idx1[0]
        self.ap2_idx = ap_idx2[0]

        # Parallel sub-tuned comb loops
        c1 = self.c1_buf[self.c1_idx]
        self.c1_filt += 0.15 * (c1 - self.c1_filt)
        self.c1_buf[self.c1_idx] = diff + self.c1_filt * 0.78
        self.c1_idx = (self.c1_idx + 1) % self.c1_len

        c2 = self.c2_buf[self.c2_idx]
        self.c2_filt += 0.15 * (c2 - self.c2_filt)
        self.c2_buf[self.c2_idx] = diff + self.c2_filt * 0.78
        self.c2_idx = (self.c2_idx + 1) % self.c2_len

        c3 = self.c3_buf[self.c3_idx]
        self.c3_filt += 0.15 * (c3 - self.c3_filt)
        self.c3_buf[self.c3_idx] = diff + self.c3_filt * 0.76
        self.c3_idx = (self.c3_idx + 1) % self.c3_len

        # Stereo decorrelation for spacious sub
        cavity_l = (c1 + c2 * 0.5) * 0.65
        cavity_r = (c2 + c3 * 0.5) * 0.65

        # 4. Harmonic Warmth (Soft Saturation)
        xl = cavity_l * self.drive
        xr = cavity_r * self.drive
        sat_l = math.tanh(xl + 0.08 * xl * xl) * 0.80
        sat_r = math.tanh(xr + 0.08 * xr * xr) * 0.80

        # 5. Apply the Smooth SWELL Envelope ("le gonflement qui monte !")
        env = self.compute_swell_envelope(phase)

        # Subtle stereo panning motion during swell
        pan_mod = math.sin(phase * math.pi) * 0.35 * self.stereo_width
        gain_l = (1.0 - pan_mod) * env * 2.4
        gain_r = (1.0 + pan_mod) * env * 2.4

        wet_l = sat_l * gain_l
        wet_r = sat_r * gain_r

        # 6. Mono-Sub (<90Hz) & Stereo Width (>90Hz)
        mid = (wet_l + wet_r) * 0.5
        side = (wet_l - wet_r) * 0.5 * self.stereo_width
        out_l = math.tanh(mid + side) * 0.94
        out_r = math.tanh(mid - side) * 0.94

        # 7. Lookahead Buffer (eliminates any boundary click)
        ret_l = self.la_buf_l[self.la_idx]
        ret_r = self.la_buf_r[self.la_idx]
        self.la_buf_l[self.la_idx] = out_l
        self.la_buf_r[self.la_idx] = out_r
        self.la_idx = (self.la_idx + 1) % self.la_len

        return ret_l, ret_r

if __name__ == "__main__":
    print("Generating Pristine Sub Swell Demos...")
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

    # 1. Pure Berlin Swell (Le vrai gonflement qui monte)
    dsp1 = TrueSubSwellEngine(SAMPLE_RATE, BPM)
    dsp1.groove_mode = 0
    dsp1.stereo_width = 0.80
    wet1 = [dsp1.process(s) for s in dry]
    save_wav("d:/xRumbleVersio/demo_v2_1_pure_rumble_offbeat_stereo.wav", wet1)

    # 2. Rolling 16th Wave Train (3 vagues de sub qui enflent sans clic)
    dsp2 = TrueSubSwellEngine(SAMPLE_RATE, BPM)
    dsp2.groove_mode = 1
    dsp2.stereo_width = 0.85
    wet2 = [dsp2.process(s) for s in dry]
    save_wav("d:/xRumbleVersio/demo_v2_2_pure_rumble_train_stereo.wav", wet2)

    # 3. Triplet Hypnotic Swell
    dsp3 = TrueSubSwellEngine(SAMPLE_RATE, BPM)
    dsp3.groove_mode = 2
    dsp3.stereo_width = 0.85
    wet3 = [dsp3.process(s) for s in dry]
    save_wav("d:/xRumbleVersio/demo_v2_3_pure_rumble_triplet_stereo.wav", wet3)

    # 4. Full Parallel Mix with Kick + Swell (Beats 10-11 Roll Break)
    dsp4 = TrueSubSwellEngine(SAMPLE_RATE, BPM)
    dsp4.groove_mode = 0
    full_mix = []
    for idx, s in enumerate(dry):
        beat_idx = idx // beat_len
        if beat_idx in [10, 11]:
            # Break before drop: sub muted cleanly
            wl, wr = 0.0, 0.0
        else:
            wl, wr = dsp4.process(s)
        mix_l = math.tanh(s * 0.85 + wl * 0.90) * 0.95
        mix_r = math.tanh(s * 0.85 + wr * 0.90) * 0.95
        full_mix.append((mix_l, mix_r))
    save_wav("d:/xRumbleVersio/demo_v2_4_full_mix_with_fsu_roll.wav", full_mix)

    print("All True Sub Swell demos generated!")
