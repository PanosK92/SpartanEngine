"""one off probe, reports the spectral shape and decay of an impulse response wav"""

import cmath
import struct
import sys
import wave


def read_wav(path):
    with wave.open(path, "rb") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        rate = handle.getframerate()
        frames = handle.getnframes()
        raw = handle.readframes(frames)

    if width != 2:
        raise SystemExit("expected 16 bit pcm, got %d bytes per sample" % width)

    samples = list(struct.unpack("<%dh" % (len(raw) // 2), raw))
    if channels > 1:
        samples = samples[::channels]

    return rate, samples


def band_energy(samples, rate, low, high):
    """goertzel style sum over a few probe bins, cheap and good enough for a shape check"""
    total = 0.0
    bins = 12
    for index in range(bins):
        freq = low + (high - low) * (index + 0.5) / bins
        omega = 2.0 * cmath.pi * freq / rate
        acc = 0j
        for position, value in enumerate(samples):
            acc += value * cmath.exp(-1j * omega * position)
        total += abs(acc) ** 2
    return total / bins


def main():
    path = sys.argv[1]
    rate, samples = read_wav(path)
    count = len(samples)
    peak = max(abs(v) for v in samples)

    print("file            %s" % path)
    print("sample rate     %d" % rate)
    print("samples         %d (%.1f ms)" % (count, 1000.0 * count / rate))
    print("peak            %d (%.1f dBFS)" % (peak, 20.0 * cmath.log10(peak / 32767.0).real))
    print("dc offset       %.1f" % (sum(samples) / float(count)))
    print("peak index      %d (%.2f ms)" % (
        samples.index(max(samples, key=abs)),
        1000.0 * samples.index(max(samples, key=abs)) / rate
    ))

    # where the energy actually sits in time, an exhaust pipe is a few ms, a room is hundreds
    total_energy = sum(float(v) * v for v in samples)
    running = 0.0
    marks = [0.5, 0.9, 0.99]
    mark_index = 0
    print("")
    for position, value in enumerate(samples):
        running += float(value) * value
        while mark_index < len(marks) and running >= marks[mark_index] * total_energy:
            print("energy %3d%% by    %6d samples (%7.1f ms)" % (
                int(marks[mark_index] * 100), position, 1000.0 * position / rate
            ))
            mark_index += 1

    # broadband gain this response applies, an uncorrelated input comes out scaled by the
    # root sum of squares of the taps, so this is what has to be matched when swapping files
    rss = (sum(float(v) * v for v in samples) ** 0.5) / 32767.0
    print("")
    print("broadband gain  %.4f (rss of taps)" % rss)

    # spectral tilt, decimate first so the naive dft stays cheap
    step = max(1, count // 20000)
    trimmed = samples[::step]
    trimmed_rate = rate / step
    print("")
    print("spectrum (relative to the 100-300 Hz band)")
    reference = band_energy(trimmed, trimmed_rate, 100.0, 300.0)
    for low, high in [
        (100.0, 300.0),
        (300.0, 800.0),
        (800.0, 1500.0),
        (1500.0, 3000.0),
        (3000.0, 6000.0),
        (6000.0, 10000.0),
    ]:
        if high > trimmed_rate * 0.5:
            break
        energy = band_energy(trimmed, trimmed_rate, low, high)
        ratio = energy / reference if reference > 0 else 0.0
        db = 10.0 * cmath.log10(ratio).real if ratio > 0 else -999.0
        print("  %5d - %5d Hz   %+7.1f dB" % (low, high, db))


main()
