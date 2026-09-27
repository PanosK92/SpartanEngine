# procedural sound set for the liminal stalker, writes 16 bit mono wavs into binaries/project/music/stalker
import os
import numpy as np
from scipy import signal
from scipy.io import wavfile

RATE = 48000
OUT = os.path.join(os.path.dirname(__file__), "..", "..", "binaries", "project", "music", "stalker")
rng = np.random.default_rng(1987)


def t_axis(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def band(x, lo, hi, order=4):
    sos = signal.butter(order, [lo, hi], btype="bandpass", fs=RATE, output="sos")
    return signal.sosfilt(sos, x)


def low(x, hz, order=4):
    sos = signal.butter(order, hz, btype="lowpass", fs=RATE, output="sos")
    return signal.sosfilt(sos, x)


def high(x, hz, order=2):
    sos = signal.butter(order, hz, btype="highpass", fs=RATE, output="sos")
    return signal.sosfilt(sos, x)


def noise(seconds):
    return rng.standard_normal(int(seconds * RATE))


def normalize(x, peak=0.95):
    return x / (np.max(np.abs(x)) + 1e-9) * peak


def fade(x, fade_in=0.005, fade_out=0.02):
    n_in, n_out = int(fade_in * RATE), int(fade_out * RATE)
    if n_in:
        x[:n_in] *= np.linspace(0, 1, n_in)
    if n_out:
        x[-n_out:] *= np.linspace(1, 0, n_out)
    return x


def loop_crossfade(x, seconds=0.25):
    n = int(seconds * RATE)
    head, tail = x[:n].copy(), x[-n:].copy()
    ramp = np.linspace(0, 1, n)
    body = x[n:].copy()
    body[-n:] = tail * (1 - ramp) + head * ramp
    return body


def write(name, x):
    os.makedirs(OUT, exist_ok=True)
    wavfile.write(os.path.join(OUT, name), RATE, (np.clip(x, -1, 1) * 32767).astype(np.int16))
    print(name, f"{len(x) / RATE:.2f}s")


# a heavy barefoot-and-weight footfall: sub thump, floor knock, grit scrape
def step():
    t = t_axis(0.7)
    freq = 38 + 55 * np.exp(-t * 30)
    thump = np.sin(2 * np.pi * np.cumsum(freq) / RATE) * np.exp(-t * 9)
    knock = band(noise(0.7), 90, 700) * np.exp(-t * 28)
    scrape_env = np.clip((t - 0.03) * 12, 0, 1) * np.exp(-np.clip(t - 0.03, 0, None) * 7)
    scrape = band(noise(0.7), 1200, 5200, 2) * scrape_env * 0.12
    creak = np.sin(2 * np.pi * (180 + 40 * np.sin(t * 9)) * t) * np.exp(-t * 6) * 0.05
    return fade(normalize(np.tanh(1.8 * (thump * 1.3 + knock * 0.8 + scrape + creak))), 0.001, 0.08)


# wet, rasping breaths with a growl underneath, the loop point sits in the silence between breaths
def breath():
    total = 5.2
    t = t_axis(total)
    out = np.zeros_like(t)

    def env(start, attack, hold, release):
        e = np.zeros_like(t)
        a = (t >= start) & (t < start + attack)
        e[a] = (t[a] - start) / attack
        h = (t >= start + attack) & (t < start + attack + hold)
        e[h] = 1
        r = (t >= start + attack + hold) & (t < start + attack + hold + release)
        e[r] = 1 - (t[r] - start - attack - hold) / release
        return e ** 1.5

    inhale = env(0.2, 0.9, 0.3, 0.25)
    exhale = env(1.9, 0.12, 0.8, 1.1)
    air_in = band(noise(total), 900, 4200) * inhale * 0.55
    air_out = band(noise(total), 250, 1900) * exhale
    rasp_mod = 0.6 + 0.4 * np.sign(np.sin(2 * np.pi * 31 * t))
    rasp = band(noise(total), 400, 2600) * rasp_mod * exhale * 0.5
    growl = np.sin(2 * np.pi * 52 * t + 0.8 * np.sin(2 * np.pi * 7 * t)) * exhale * 0.55
    growl += np.sin(2 * np.pi * 78 * t) * exhale * 0.25
    out = air_in + air_out + rasp + np.tanh(growl * 2) * 0.6
    return loop_crossfade(normalize(out, 0.9), 0.3)


# stab of detuned strings when it first catches sight of you
def spotted():
    t = t_axis(2.4)
    x = np.zeros_like(t)
    for f in (233, 247, 262, 349, 370, 494, 523, 740):
        detune = 1 + rng.uniform(-0.006, 0.006)
        vib = 1 + 0.01 * np.sin(2 * np.pi * rng.uniform(5, 7) * t)
        x += signal.sawtooth(2 * np.pi * np.cumsum(f * detune * vib) / RATE)
    x = band(x, 180, 6000) * np.exp(-t * 1.4) * np.clip(t * 60, 0, 1)
    hit = low(noise(2.4), 160) * np.exp(-t * 5) * 4
    sub = np.sin(2 * np.pi * np.cumsum(60 - 25 * t) / RATE) * np.exp(-t * 1.2) * 0.7
    return fade(normalize(np.tanh(x * 0.25 + hit + sub)), 0.001, 0.3)


# the catch: screaming cluster falling in pitch, torn with distortion
def scream():
    t = t_axis(2.8)
    x = np.zeros_like(t)
    bend = 1 - 0.35 * (t / t[-1]) ** 1.5
    for f in (410, 437, 620, 655, 880, 931, 1320):
        jitter = 1 + 0.03 * low(noise(2.8), 12) * 3
        x += signal.sawtooth(2 * np.pi * np.cumsum(f * bend * jitter) / RATE)
    voice = band(x, 300, 7000) * 0.3
    tear = high(noise(2.8), 2500) * (0.3 + 0.7 * np.abs(np.sin(2 * np.pi * 17 * t)))
    body = low(noise(2.8), 220) * 3
    sub = np.sin(2 * np.pi * np.cumsum(70 * bend) / RATE) * 1.2
    env = np.clip(t * 200, 0, 1) * np.exp(-t * 0.9)
    return fade(normalize(np.tanh((voice + tear * 0.6 + body + sub) * env * 1.8)), 0.0005, 0.4)


# two beat heart cycle, pitching the source up speeds it up
def heartbeat():
    t = t_axis(0.9)

    def beat(start, strength):
        tt = np.clip(t - start, 0, None)
        on = (t >= start).astype(float)
        f = 48 + 30 * np.exp(-tt * 40)
        return np.sin(2 * np.pi * np.cumsum(f * on) / RATE) * np.exp(-tt * 16) * on * strength

    x = beat(0.0, 1.0) + beat(0.24, 0.7)
    return fade(normalize(low(x, 180)), 0.001, 0.02)


# low dread bed with slow beating, faded in as it gets close
def drone():
    total = 8.0
    t = t_axis(total)
    x = np.sin(2 * np.pi * 37 * t) + np.sin(2 * np.pi * 37.5 * t) * 0.8
    x += np.sin(2 * np.pi * 74.4 * t) * 0.3 * (0.5 + 0.5 * np.sin(2 * np.pi * 0.25 * t))
    x += band(noise(total), 60, 400) * 0.25 * (0.6 + 0.4 * np.sin(2 * np.pi * 0.125 * t))
    x += signal.sawtooth(2 * np.pi * 111 * t) * 0.04
    return loop_crossfade(normalize(low(x, 900), 0.8), 0.5)


# hinge creak over a soft handle click
def door_open():
    t = t_axis(1.3)
    click = band(noise(1.3), 1500, 6000) * np.exp(-t * 90) * 0.6
    creak_f = 210 + 90 * np.sin(2 * np.pi * 0.9 * t) + 40 * low(noise(1.3), 6) * 8
    pulses = (np.sin(2 * np.pi * np.cumsum(creak_f / 6.0) / RATE) > 0.6).astype(float)
    creak = band(signal.sawtooth(2 * np.pi * np.cumsum(creak_f) / RATE) * pulses, 300, 3200)
    env = np.clip((t - 0.08) * 6, 0, 1) * np.exp(-np.clip(t - 0.3, 0, None) * 2.2)
    return fade(normalize(click + creak * env * 0.5, 0.8), 0.001, 0.2)


# leaf meeting the frame and the latch catching
def door_close():
    t = t_axis(0.9)
    thud = np.sin(2 * np.pi * np.cumsum(70 + 60 * np.exp(-t * 25)) / RATE) * np.exp(-t * 14)
    body = band(noise(0.9), 120, 900) * np.exp(-t * 22)
    latch_t = np.clip(t - 0.07, 0, None)
    latch = band(noise(0.9), 2500, 8000) * np.exp(-latch_t * 120) * (t >= 0.07) * 0.5
    return fade(normalize(np.tanh(thud + body * 0.7 + latch)), 0.001, 0.1)


# a door thrown open hard enough to hit the wall
def door_slam():
    t = t_axis(1.6)
    boom = np.sin(2 * np.pi * np.cumsum(55 + 90 * np.exp(-t * 18)) / RATE) * np.exp(-t * 5)
    crack = band(noise(1.6), 300, 5000) * np.exp(-t * 30) * 1.5
    rattle_env = np.exp(-t * 4) * (0.5 + 0.5 * np.sign(np.sin(2 * np.pi * 23 * t)))
    rattle = band(noise(1.6), 800, 3000) * rattle_env * 0.25
    hit2_t = np.clip(t - 0.22, 0, None)
    hit2 = band(noise(1.6), 150, 1500) * np.exp(-hit2_t * 25) * (t >= 0.22) * 0.8
    return fade(normalize(np.tanh(2.2 * (boom + crack + rattle + hit2))), 0.0005, 0.3)


write("door_open.wav", door_open())
write("door_close.wav", door_close())
write("door_slam.wav", door_slam())
write("stalker_step.wav", step())
write("stalker_breath.wav", breath())
write("stalker_spotted.wav", spotted())
write("stalker_scream.wav", scream())
write("heartbeat.wav", heartbeat())
write("dread_drone.wav", drone())
