"""
generate_audio_demos_v2.py
Simulation fidèle du Moteur Rumble Versio v2.3 (Daisy Seed / Cortex-M7):
1. Matrice des 3 Tranches avec DOWNBEAT au centre (Tone) :
   - Tone (KNOB_2) : Downbeat Level (Temps 1 / Sub du kick).
   - Blend (KNOB_0) : Tap 1 Level (16e #2, pan gauche).
   - Regen (KNOB_1) : Tap 2 / Offbeat Level (16e #3, pan droite).
   - Deadband strict < 8% : 0.000000 immédiat si les 3 potards du haut sont à zéro.
2. Horloge Calée sur le Jack FSU (Clock Gate Externe) :
   - Synchronisation précise sur les pulses d'horloge.
   - Switch 1 (Ajustement Décalage / Micro-timing) :
       * UP (0)     : Ahead / Rush (-12 ms, groove tiré en avant)
       * CENTER (1) : On-Beat (0 ms, calage parfait)
       * DOWN (2)   : Laid-Back Drag (+18 ms, retard chaloupé / swing techno)
3. Filtre Résonant SVF 24 dB/oct Ultra-Audible (Speed & Index) :
   - Speed (KNOB_5) : Balayage franc de 30 Hz à 3200 Hz.
   - Index (KNOB_4) : Résonance Q de 0.707 à 6.5.
4. Bouton FSU Décorrélé : Effet Freeze Wash (Reverb + Delay Infini 99.5%).
"""

import math
import struct
import wave

SAMPLE_RATE = 48000
BPM = 132.0

def make_punchy_kick(duration_sec=0.45):
    num_samples = int(SAMPLE_RATE * duration_sec)
    samples = [0.0] * num_samples
    phase = 0.0
    for i in range(num_samples):
        t = i / float(SAMPLE_RATE)
        pitch = 48.0 + (150.0 - 48.0) * math.exp(-t * 36.0)
        phase += 2.0 * math.pi * pitch / SAMPLE_RATE
        body = math.sin(phase)
        click_phase = 2.0 * math.pi * 1200.0 * t
        click = math.sin(click_phase) * math.exp(-t * 350.0) * 0.35
        attack = 1.0 - math.exp(-t * 2000.0)
        decay = math.exp(-t * 8.0)
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

class StateVariableFilterPy:
    def __init__(self):
        self.reset()

    def reset(self):
        self.low = 0.0
        self.band = 0.0
        self.f = 0.01
        self.q_inv = 1.0

    def set_params(self, fc, q, fs):
        fc = max(20.0, min(fc, fs * 0.40))
        q = max(0.5, min(q, 10.0))
        self.f = 2.0 * math.sin(math.pi * fc / fs)
        self.q_inv = 1.0 / q

    def process(self, x):
        for _ in range(2):
            self.low += (self.f * 0.5) * self.band
            high = x - self.low - self.q_inv * self.band
            self.band += (self.f * 0.5) * high
        return self.low

class CascadedSVF4PolePy:
    def __init__(self):
        self.svf1 = StateVariableFilterPy()
        self.svf2 = StateVariableFilterPy()

    def reset(self):
        self.svf1.reset()
        self.svf2.reset()

    def set_params(self, fc, q, fs):
        self.svf1.set_params(fc, 0.7071, fs)
        self.svf2.set_params(fc, q, fs)

    def process(self, x):
        return self.svf2.process(self.svf1.process(x))

class BiquadPy:
    def __init__(self):
        self.reset()

    def reset(self):
        self.s1 = 0.0; self.s2 = 0.0
        self.b0 = 0.0; self.b1 = 0.0; self.b2 = 0.0
        self.a1 = 0.0; self.a2 = 0.0

    def set_coeffs(self, b0, b1, b2, a1, a2):
        self.b0 = b0; self.b1 = b1; self.b2 = b2
        self.a1 = a1; self.a2 = a2

    def process(self, x):
        out = self.b0 * x + self.s1
        self.s1 = self.b1 * x - self.a1 * out + self.s2
        self.s2 = self.b2 * x - self.a2 * out
        return out

class AllpassPy:
    def __init__(self, length, coeff):
        self.len = length
        self.coeff = coeff
        self.buf = [0.0] * length
        self.idx = 0

    def clear(self):
        self.buf = [0.0] * self.len
        self.idx = 0

    def process(self, x):
        buf_out = self.buf[self.idx]
        vn = x - self.coeff * buf_out
        out = buf_out + self.coeff * vn
        self.buf[self.idx] = vn
        self.idx = (self.idx + 1) % self.len
        return out

class CombPy:
    def __init__(self, length, damp):
        self.len = length
        self.damp = damp
        self.buf = [0.0] * length
        self.idx = 0
        self.filt = 0.0

    def clear(self):
        self.buf = [0.0] * self.len
        self.idx = 0
        self.filt = 0.0

    def process(self, x, fb):
        buf_out = self.buf[self.idx]
        self.filt = buf_out * (1.0 - self.damp) + self.filt * self.damp
        fb = min(0.998, fb)
        self.buf[self.idx] = x + self.filt * fb
        self.idx = (self.idx + 1) % self.len
        return buf_out

class BohmGrooveEnginePy:
    def __init__(self, sample_rate=48000, bpm=132.0):
        self.fs = sample_rate
        self.bpm = bpm
        self.beat_samples = int(self.fs * (60.0 / bpm))

        # Controls mapped to real hardware pins
        self.k_blend = 0.0  # KNOB_0: Blend (Tap 1 / 16e #2)
        self.k_tone = 0.85  # KNOB_2: Tone (Downbeat / Temps 1)
        self.k_regen = 0.80 # KNOB_1: Regen (Tap 2 / Offbeat)
        self.k_speed = 0.50 # KNOB_5: Speed (Cutoff 30 Hz à 3200 Hz)
        self.k_index = 0.45 # KNOB_4: Index (Resonance Q 0.707 à 6.5)
        self.k_size = 0.0   # KNOB_3: Size (Reverb 0=Bypass, 1=Grand espace)
        self.k_dense = 0.35 # KNOB_6: Dense (Drive 1.0x à 4.2x)

        self.timing_offset_mode = 1 # Switch 1: 0=Ahead (-12ms), 1=On-beat, 2=Drag (+18ms)
        self.sat_mode = 1           # Switch 0: 0=Sub Pur, 1=Warm Tape, 2=Fat Crunch
        self.fsu_freeze_active = False
        self.fsu_freeze_env = 0.0

        # Circular delay buffer
        self.delay_buf_len = 32768
        self.delay_buf = [0.0] * self.delay_buf_len
        self.write_idx = 0

        # Input conditioner at 500 Hz
        self.pre_lp = 0.0
        self.pre_coeff = 1.0 - math.exp(-2.0 * math.pi * 500.0 / self.fs)

        # SubReverb
        self.ap1 = AllpassPy(149, 0.70)
        self.ap2 = AllpassPy(223, 0.70)
        self.ap3 = AllpassPy(367, 0.65)
        self.ap4 = AllpassPy(521, 0.60)
        self.comb_l1 = CombPy(887,  0.35)
        self.comb_l2 = CombPy(1181, 0.35)
        self.comb_r1 = CombPy(1013, 0.35)
        self.comb_r2 = CombPy(1327, 0.35)

        # 4-Pole SVF Filters
        self.svf_l = CascadedSVF4PolePy()
        self.svf_r = CascadedSVF4PolePy()
        self.hp_sub_l = BiquadPy(); self.hp_sub_r = BiquadPy()
        self.hp_side = BiquadPy()

        self.setup_hp(self.hp_sub_l, 25.0)
        self.setup_hp(self.hp_sub_r, 25.0)
        self.setup_hp(self.hp_side, 90.0)

        self.phase_samples = 0
        self.was_silent = False

    def setup_hp(self, bq, fc):
        w = 2.0 * math.pi * fc / self.fs
        sin_w = math.sin(w)
        cos_w = math.cos(w)
        alpha = sin_w / (2.0 * 0.7071)
        a0 = 1.0 + alpha
        b0 = ((1.0 + cos_w) * 0.5) / a0
        b1 = (-(1.0 + cos_w)) / a0
        b2 = ((1.0 + cos_w) * 0.5) / a0
        a1 = (-2.0 * cos_w) / a0
        a2 = (1.0 - alpha) / a0
        bq.set_coeffs(b0, b1, b2, a1, a2)

    def read_tap(self, delay_samples):
        rpos = float(self.write_idx) - delay_samples
        while rpos < 0.0:
            rpos += self.delay_buf_len
        while rpos >= self.delay_buf_len:
            rpos -= self.delay_buf_len
        i0 = int(rpos)
        i1 = (i0 + 1) % self.delay_buf_len
        frac = rpos - float(i0)
        return self.delay_buf[i0] + frac * (self.delay_buf[i1] - self.delay_buf[i0])

    def trigger_clock(self):
        self.phase_samples = 0

    def reset_buffers(self):
        self.ap1.clear(); self.ap2.clear(); self.ap3.clear(); self.ap4.clear()
        self.comb_l1.clear(); self.comb_l2.clear(); self.comb_r1.clear(); self.comb_r2.clear()
        self.svf_l.reset(); self.svf_r.reset()
        self.hp_sub_l.reset(); self.hp_sub_r.reset(); self.hp_side.reset()

    def process_sample(self, in_mono):
        # 1. Volume mapping des 3 tranches avec silence strict < 8%
        def map_level(k):
            if k < 0.08: return 0.0
            norm = (k - 0.08) / 0.92
            return norm * norm

        t1_vol = map_level(self.k_blend)
        down_vol = map_level(self.k_tone)
        t2_vol = map_level(self.k_regen)

        # Silence strict si les 3 potards du haut sont à zéro !
        if t1_vol <= 0.0 and down_vol <= 0.0 and t2_vol <= 0.0 and not self.fsu_freeze_active and self.fsu_freeze_env <= 0.001:
            if not self.was_silent:
                self.reset_buffers()
                self.was_silent = True
            return 0.0, 0.0
        self.was_silent = False

        phase = float(self.phase_samples) / float(self.beat_samples)
        self.phase_samples = (self.phase_samples + 1) % self.beat_samples

        # Input conditioner
        self.pre_lp += self.pre_coeff * (in_mono - self.pre_lp)
        sub_in = self.pre_lp

        self.delay_buf[self.write_idx] = sub_in
        self.write_idx = (self.write_idx + 1) % self.delay_buf_len

        # FSU Freeze Envelope
        target_freeze = 1.0 if self.fsu_freeze_active else 0.0
        coeff = 0.002 if self.fsu_freeze_active else 0.0002
        self.fsu_freeze_env += (target_freeze - self.fsu_freeze_env) * coeff

        # Micro-timing Offset (Switch 1)
        offset_samples = 0.0
        if self.timing_offset_mode == 0:
            offset_samples = -0.012 * self.fs # -12 ms
        elif self.timing_offset_mode == 2:
            offset_samples = 0.018 * self.fs  # +18 ms

        bf = float(self.beat_samples)
        d_downbeat = max(64.0, bf * 1.000 + offset_samples)
        d_tap1     = max(64.0, bf * 0.250 + offset_samples)
        d_tap2     = max(64.0, bf * 0.500 + offset_samples)

        t_down = self.read_tap(d_downbeat) * down_vol if down_vol > 0.0 else 0.0
        t1 = self.read_tap(d_tap1) * t1_vol if t1_vol > 0.0 else 0.0
        t2 = self.read_tap(d_tap2) * t2_vol if t2_vol > 0.0 else 0.0

        # Stereo distribution
        mix_l = t_down * 0.707 + t1 * 0.85 + t2 * 0.15
        mix_r = t_down * 0.707 + t1 * 0.15 + t2 * 0.85

        # Reverb & FSU Freeze Wash
        if self.k_size < 0.04 and self.fsu_freeze_env < 0.01:
            fx_in_l = mix_l
            fx_in_r = mix_r
        else:
            diff = self.ap1.process((mix_l + mix_r) * 0.5)
            diff = self.ap2.process(diff)
            diff = self.ap3.process(diff)
            diff = self.ap4.process(diff)

            normal_fb = 0.20 + self.k_size * 0.55
            rev_fb = normal_fb * (1.0 - self.fsu_freeze_env) + 0.995 * self.fsu_freeze_env

            l1 = self.comb_l1.process(diff, rev_fb)
            l2 = self.comb_l2.process(diff, rev_fb)
            r1 = self.comb_r1.process(diff, rev_fb)
            r2 = self.comb_r2.process(diff, rev_fb)

            rev_l = (l1 + l2) * 0.5
            rev_r = (r1 + r2) * 0.5

            wet = min(1.0, self.k_size + self.fsu_freeze_env * 0.85)
            fx_in_l = mix_l * (1.0 - 0.35 * wet) + rev_l * (1.0 * wet)
            fx_in_r = mix_r * (1.0 - 0.35 * wet) + rev_r * (1.0 * wet)

        # Saturation Stage
        drive = 1.0 + math.pow(self.k_dense, 1.30) * 3.2

        def compute_sat(x, mode):
            xd = x * drive
            if mode == 0:
                norm = 1.0 / math.sqrt(0.8 + 0.4 * drive)
                return math.tanh(xd) * norm
            elif mode == 1:
                asym = xd + 0.25 * (xd * xd)
                norm = 1.0 / math.sqrt(0.85 + 0.50 * drive)
                return math.tanh(asym) * norm
            else:
                driven = xd * 1.15
                if driven > 0.85:
                    crunch = 0.85 + 0.15 * math.tanh((driven - 0.85) * 2.0)
                elif driven < -0.75:
                    crunch = -0.75 + 0.15 * math.tanh((driven + 0.75) * 2.0)
                else:
                    crunch = driven - 0.18 * (driven * driven * driven)
                norm = 1.0 / (0.90 + 0.35 * drive)
                return crunch * norm

        sat_l = compute_sat(fx_in_l, self.sat_mode)
        sat_r = compute_sat(fx_in_r, self.sat_mode)

        # SVF 24 dB/oct Filter (30 Hz to 3200 Hz!)
        fc = 30.0 * math.pow(3200.0 / 30.0, math.pow(self.k_speed, 1.25))
        q = 0.7071 + math.pow(self.k_index, 1.40) * 5.8
        self.svf_l.set_params(fc, q, self.fs)
        self.svf_r.set_params(fc, q, self.fs)

        hp_l = self.hp_sub_l.process(sat_l)
        hp_r = self.hp_sub_r.process(sat_r)
        flt_l = self.svf_l.process(hp_l)
        flt_r = self.svf_r.process(hp_r)

        # Sidechain Ducking
        normal_duck = 1.0
        if phase < 0.08:
            normal_duck = 0.0
        elif phase < 0.24:
            p = (phase - 0.08) / 0.16
            normal_duck = p * p
        if phase > 0.94:
            p = (1.0 - phase) / 0.06
            normal_duck *= (p * p)

        duck = normal_duck * (1.0 - self.fsu_freeze_env * 0.70) + 0.70 * self.fsu_freeze_env

        ducked_l = flt_l * duck
        ducked_r = flt_r * duck

        # Mid/Side Protection Mono (<90Hz mono)
        mid = (ducked_l + ducked_r) * 0.5
        side = (ducked_l - ducked_r) * 0.5
        filt_side = self.hp_side.process(side)

        out_l = math.tanh(mid + filt_side) * 0.95
        out_r = math.tanh(mid - filt_side) * 0.95
        return out_l, out_r

def run_simulation(duration_bars=4, bpm=132.0, config_fn=None):
    kick_sample = make_punchy_kick(0.40)
    beat_samples = int(SAMPLE_RATE * (60.0 / bpm))
    total_beats = duration_bars * 4
    total_samples = total_beats * beat_samples

    kick_track = [0.0] * total_samples
    for b in range(total_beats):
        start_idx = b * beat_samples
        for k in range(len(kick_sample)):
            if start_idx + k < total_samples:
                kick_track[start_idx + k] = kick_sample[k]

    engine = BohmGrooveEnginePy(sample_rate=SAMPLE_RATE, bpm=bpm)
    rumble_stereo = []
    mix_stereo = []

    for i in range(total_samples):
        # Clock trigger on FSU Jack at start of each beat
        if i % beat_samples == 0:
            engine.trigger_clock()

        if config_fn:
            config_fn(engine, i, total_samples)

        k_in = kick_track[i]
        r_l, r_r = engine.process_sample(k_in)
        rumble_stereo.append((r_l, r_r))
        m_l = k_in * 0.85 + r_l * 0.85
        m_r = k_in * 0.85 + r_r * 0.85
        mix_stereo.append((math.tanh(m_l), math.tanh(m_r)))

    return rumble_stereo, mix_stereo

def main():
    print("=== Generating Reboot v2.3 Audio Demos ===")

    # Demo 1: Downbeat & Offbeat Sculpting (Tone & Regen active, Blend muted)
    print("\n1. Demo: Downbeat & Offbeat Groove (Tone & Regen active, Blend muted, Warm Tape)")
    def cfg_1(eng, i, total):
        eng.k_blend = 0.0  # Tap 1 muted
        eng.k_tone = 0.80   # Downbeat sub body loud and clear!
        eng.k_regen = 0.85  # Offbeat rolling tap
        eng.k_speed = 0.45  # Warm resonant cutoff
        eng.k_index = 0.35  # Filter Q
        eng.k_size = 0.0    # Pure punchy dry taps
        eng.k_dense = 0.35  # Warm Tape drive
        eng.timing_offset_mode = 1 # On-beat
        eng.sat_mode = 1    # Warm Tape
    r1, m1 = run_simulation(duration_bars=4, config_fn=cfg_1)
    save_wav("demo_v2_1_offbeat_pump.wav", r1)
    save_wav("demo_v2_1_offbeat_pump_mix.wav", m1)
    save_wav("demo_v2_1_pure_rumble_offbeat_stereo.wav", r1)

    # Demo 2: Rolling 16th Warehouse (Blend, Tone, Regen all sculpted, Reverb Size = 0.45)
    print("\n2. Demo: Rolling 16th Warehouse (Blend, Tone, Regen, Dense Warehouse Reverb)")
    def cfg_2(eng, i, total):
        eng.k_blend = 0.70  # Tap 1 (panned Left)
        eng.k_tone = 0.60   # Downbeat (Center)
        eng.k_regen = 0.75  # Tap 2 (panned Right)
        eng.k_speed = 0.55  # Cutoff
        eng.k_index = 0.40  # Filter Q
        eng.k_size = 0.45   # Reverb Space
        eng.k_dense = 0.40  # Moderate drive
        eng.timing_offset_mode = 1 # On-beat
        eng.sat_mode = 1    # Warm Tape
    r2, m2 = run_simulation(duration_bars=4, config_fn=cfg_2)
    save_wav("demo_v2_2_rolling_16th_warehouse.wav", r2)
    save_wav("demo_v2_2_rolling_16th_warehouse_mix.wav", m2)
    save_wav("demo_v2_2_pure_rumble_train_stereo.wav", r2)

    # Demo 3: Audible Resonant SVF Sweep (30 Hz to 2400 Hz, Fat Crunch)
    print("\n3. Demo: SVF Resonant Filter Sweep (Speed swept from 40 Hz to 2200 Hz, Fat Crunch)")
    def cfg_3(eng, i, total):
        eng.k_blend = 0.65
        eng.k_tone = 0.75
        eng.k_regen = 0.70
        progress = float(i) / float(total)
        # Audible sweep from 40 Hz to 2200 Hz
        sweep = 0.10 + 0.80 * (0.5 - 0.5 * math.cos(2.0 * math.pi * progress))
        eng.k_speed = sweep
        eng.k_index = 0.80  # Screaming resonant Q!
        eng.k_size = 0.20
        eng.k_dense = 0.55  # Fat Crunch drive
        eng.timing_offset_mode = 1
        eng.sat_mode = 2    # Fat Crunch
    r3, m3 = run_simulation(duration_bars=4, config_fn=cfg_3)
    save_wav("demo_v2_3_triplet_hypnotic.wav", r3)
    save_wav("demo_v2_3_triplet_hypnotic_mix.wav", m3)
    save_wav("demo_v2_3_pure_rumble_triplet_stereo.wav", r3)

    # Demo 4: FSU Freeze Wash & Strict Silence Test
    print("\n4. Demo: Strict Silence -> Groove -> FSU Infinite Freeze Wash build-up")
    def cfg_4(eng, i, total):
        bar = int((float(i) / float(total)) * 4.0)
        if bar == 0:
            # Bar 1: STRICT SILENCE TEST (Blend, Tone, Regen all = 0.0)
            eng.k_blend = 0.0
            eng.k_tone = 0.0
            eng.k_regen = 0.0
            eng.k_size = 0.50 # Even with reverb size up, TOTAL SILENCE!
            eng.k_dense = 0.60
            eng.fsu_freeze_active = False
        elif bar == 1:
            # Bar 2: Tight Groove cuts in
            eng.k_blend = 0.60
            eng.k_tone = 0.85
            eng.k_regen = 0.75
            eng.k_size = 0.25
            eng.k_dense = 0.40
            eng.k_speed = 0.50
            eng.fsu_freeze_active = False
        else:
            # Bars 3 & 4: FSU PRESSED -> Infinite Freeze Wash / Reverb swell!
            eng.k_blend = 0.60
            eng.k_tone = 0.85
            eng.k_regen = 0.75
            eng.k_size = 0.40
            eng.k_dense = 0.50
            eng.k_speed = 0.65
            eng.fsu_freeze_active = True # Infinite Freeze feedback!
        eng.timing_offset_mode = 1
        eng.sat_mode = 2 # Fat Crunch
    r4, m4 = run_simulation(duration_bars=4, config_fn=cfg_4)
    save_wav("demo_v2_4_fsu_ambient_wash.wav", r4)
    save_wav("demo_v2_4_fsu_ambient_wash_mix.wav", m4)
    save_wav("demo_v2_4_full_mix_with_fsu_roll.wav", m4)

    print("\nAll WAV files regenerated successfully!")

if __name__ == "__main__":
    main()
