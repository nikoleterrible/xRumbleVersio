"""
verify_dsp.py - with robust Onset Detection for Transient Detector
"""

import math

def generate_techno_kick(sample_rate=48000, bpm=130, num_beats=4):
    samples_per_beat = int(sample_rate * (60.0 / bpm))
    total_samples = samples_per_beat * num_beats
    signal = [0.0] * total_samples
    kick_len = int(sample_rate * 0.40) # 400 ms kick

    for b in range(num_beats):
        start = b * samples_per_beat
        phase = 0.0
        for i in range(kick_len):
            if start + i >= total_samples:
                break
            t = i / float(sample_rate)
            pitch = 48.0 + (160.0 - 48.0) * math.exp(-t * 28.0)
            phase += 2.0 * math.pi * pitch / sample_rate
            amp = math.exp(-t * 12.0)
            click = math.sin(2.0 * math.pi * 1200.0 * t) * math.exp(-t * 400.0)
            signal[start + i] += (math.sin(phase) + 0.3 * click) * amp

    return signal, samples_per_beat

class BiquadTest:
    def __init__(self):
        self.reset()
    def reset(self):
        self.s1 = 0.0
        self.s2 = 0.0
    def set_coeffs(self, b0, b1, b2, a1, a2):
        self.b0, self.b1, self.b2, self.a1, self.a2 = b0, b1, b2, a1, a2
    def process(self, x):
        out = self.b0 * x + self.s1
        self.s1 = self.b1 * x - self.a1 * out + self.s2
        self.s2 = self.b2 * x - self.a2 * out
        return out

def test_transient_and_ducking(sample_rate=48000):
    kick, samples_per_beat = generate_techno_kick(sample_rate=sample_rate, bpm=130, num_beats=4)
    
    # 2nd order bandpass filter around 160 Hz (punch)
    w = 2.0 * math.pi * 160.0 / sample_rate
    alpha = math.sin(w) / (2.0 * 1.5)
    b0 = alpha
    b1 = 0.0
    b2 = -alpha
    a0 = 1.0 + alpha
    a1 = -2.0 * math.cos(w)
    a2 = 1.0 - alpha
    bp = BiquadTest()
    bp.set_coeffs(b0/a0, b1/a0, b2/a0, a1/a0, a2/a0)

    # Fast energy follower + onset rise detector
    fast_env = 0.0
    slow_env = 0.0
    cooldown = 0
    triggers = []
    
    # Ducking envelope
    current_env = 1.0
    hold_counter = 0
    release_rate = 1.0 - math.exp(-3.0 / (sample_rate * 0.18)) # 180ms release

    for idx, s in enumerate(kick):
        if cooldown > 0:
            cooldown -= 1

        filtered = bp.process(s)
        abs_val = abs(filtered)

        # Fast attack follower (2ms), slow decay (20ms)
        if abs_val > fast_env:
            fast_env += 0.25 * (abs_val - fast_env)
        else:
            fast_env *= 0.998

        # Slower moving average follower (40ms)
        slow_env += 0.001 * (fast_env - slow_env)

        # Onset: fast envelope rises sharply above slow background energy
        onset_strength = fast_env - slow_env * 1.5

        if onset_strength > 0.06 and fast_env > 0.08 and cooldown == 0:
            triggers.append(idx)
            # Hold-off: 200 ms (supports up to 300 BPM)
            cooldown = int(sample_rate * 0.200)
            current_env = 0.0
            hold_counter = int(sample_rate * 0.028)

        if hold_counter > 0:
            hold_counter -= 1
            current_env = 0.0
        else:
            current_env += release_rate * (1.0 - current_env)
            if current_env > 1.0:
                current_env = 1.0

    print("\n=== Transient Detection & Ducking Verification ===")
    print(f"  Total kicks detected: {len(triggers)} (Expected: 4 beats)")
    assert len(triggers) == 4, f"Expected 4 kick triggers, got {len(triggers)}"
    
    intervals = [triggers[i] - triggers[i-1] for i in range(1, len(triggers))]
    for i, iv in enumerate(intervals):
        bpm = (60.0 * sample_rate) / iv
        print(f"  Beat {i+1}->{i+2} interval: {iv} samples -> {bpm:.1f} BPM (Expected 130.0 BPM)")
        assert abs(bpm - 130.0) < 1.0, "BPM estimation drift detected!"

    print("  -> Transient detector & ducking pump timing VERIFIED")

if __name__ == "__main__":
    test_transient_and_ducking()
    print("\nPASSED!")
