"""Compare PCM16 reference and synthesized WAVs; requires numpy, never used by the game.

Usage: python tools/audio_tests/analyze_reference.py file.wav --start 2 --end 3
Run on isolated portions: road/engine noise in a skid recording biases low bands.
These measurements describe timbre differences; they do not certify realism.
"""
import argparse
import json
import wave

import numpy as np


def measure(path, start, end):
    with wave.open(path, "rb") as wav:
        if wav.getsampwidth() != 2:
            raise ValueError("Convert reference to PCM16 WAV first")
        rate = wav.getframerate()
        signal = np.frombuffer(wav.readframes(wav.getnframes()), "<i2")
        signal = signal.reshape(-1, wav.getnchannels()).mean(axis=1) / 32768.0
    signal = signal[int(start * rate):int(end * rate) if end is not None else None]
    size = 2048
    if len(signal) < size:
        raise ValueError("Select at least 2048 samples")
    frames = np.lib.stride_tricks.sliding_window_view(signal, size)[::512]
    spectrum = abs(np.fft.rfft(frames * np.hanning(size))) ** 2
    frequencies = np.fft.rfftfreq(size, 1 / rate)
    power = spectrum.sum(axis=0)
    total = max(power[frequencies >= 80].sum(), 1e-20)
    bands = [(80, 400), (400, 800), (800, 1600), (1600, 3200), (3200, 6400)]
    pitches, harmonics = [], []
    for row in spectrum:
        fundamental = frequencies[np.argmax(row * ((frequencies > 850) & (frequencies < 1500)))]
        pitches.append(fundamental)
        first = row[abs(frequencies - fundamental) < 90].sum()
        second = row[abs(frequencies - 2 * fundamental) < 130].sum()
        harmonics.append(second / max(first, 1e-20))
    return {
        "path": path,
        "seconds": len(signal) / rate,
        "rms": float(np.sqrt(np.mean(signal ** 2))),
        "band_energy_fraction": {
            f"{lo}-{hi}": float(power[(frequencies >= lo) & (frequencies < hi)].sum() / total)
            for lo, hi in bands
        },
        "dominant_850_1500_hz_percentiles_10_50_90": np.percentile(pitches, [10, 50, 90]).tolist(),
        "second_to_fundamental_power_percentiles_10_50_90": np.percentile(harmonics, [10, 50, 90]).tolist(),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav")
    parser.add_argument("--start", type=float, default=0)
    parser.add_argument("--end", type=float)
    args = parser.parse_args()
    print(json.dumps(measure(args.wav, args.start, args.end), indent=2))
