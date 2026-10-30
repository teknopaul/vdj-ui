#!/usr/bin/env python3
# Writes a synthetic 16 bit wav for the analysis tests:  gen_beats.py FILE BPM SECONDS [stereo] [offset_s] [level]
# kicks (a decaying 60 Hz sine) on every beat plus a hat (noise burst) on the off beats; BPM 0 = silence, -1 = noise.
import math, random, struct, sys, wave

path, bpm, secs = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
stereo = len(sys.argv) > 4 and sys.argv[4] == "stereo"
offset = float(sys.argv[5]) if len(sys.argv) > 5 else 0.1
level = float(sys.argv[6]) if len(sys.argv) > 6 else 0.9
sr = 44100
rnd = random.Random(12345)
out = []
period = 60.0 / bpm if bpm > 0 else 0
for i in range(int(sr * secs)):
    t = i / sr
    v = 0.0
    if bpm < 0:
        v = rnd.uniform(-0.5, 0.5)
    elif bpm > 0 and t >= offset:
        ph = (t - offset) % period
        if ph < 0.1:
            v += level * math.exp(-ph * 40) * math.sin(2 * math.pi * 60 * ph)
        hat = (t - offset + period / 2) % period
        if hat < 0.02:
            v += 0.15 * level * rnd.uniform(-1, 1) * math.exp(-hat * 150)
    s = int(max(-1, min(1, v)) * 32767)
    out.append(s)
    if stereo:
        out.append(int(s * 0.5))
w = wave.open(path, "wb")
w.setnchannels(2 if stereo else 1)
w.setsampwidth(2)
w.setframerate(sr)
w.writeframes(struct.pack("<%dh" % len(out), *out))
w.close()
