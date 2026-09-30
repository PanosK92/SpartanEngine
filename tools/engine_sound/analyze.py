"""
Objective look at the procedural engine sound: renders scripted scenarios for every car through
the live synthesizer and turns them into spectrograms, engine order maps, cycle waveforms and a
metrics file, so a change can be judged by numbers and pictures and not only by ear.

    python tools/engine_sound/analyze.py render  [--out DIR] [--cars a,b] [--scenarios a,b] [--view chase]
    python tools/engine_sound/analyze.py analyze DIR
    python tools/engine_sound/analyze.py compare DIR_BEFORE DIR_AFTER [--out DIR]
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.io import wavfile
from scipy.signal import butter, sosfiltfilt, stft

root = Path(__file__).resolve().parent
repo = root.parent.parent
cars_dir = repo / "binaries" / "project" / "cars"
all_scenarios = ["idle", "start", "wot_sweep", "part_sweep", "overrun", "limiter", "blips", "pulls"]
# in-game captures are usually dyno pulls, only their rising, on-throttle stretches are order analysed
sweep_scenarios = {"wot_sweep", "part_sweep", "capture"}


def load_run(base):
    rate, audio = wavfile.read(str(base) + ".wav")
    # in-game captures are 16 bit, offline renders are float
    scale = 1.0 / 32768.0 if audio.dtype == np.int16 else 1.0
    audio = audio.astype(np.float64) * scale
    mono = audio.mean(axis=1) if audio.ndim == 2 else audio
    data = np.genfromtxt(str(base) + ".csv", delimiter=",", names=True)
    meta = json.loads(Path(str(base) + ".json").read_text())
    return rate, mono, data, meta


def a_weighting_db(freq):
    f2 = np.maximum(freq, 1e-3) ** 2
    ra = (12194.0**2 * f2**2) / ((f2 + 20.6**2) * np.sqrt((f2 + 107.7**2) * (f2 + 737.9**2)) * (f2 + 12194.0**2))
    return 20.0 * np.log10(np.maximum(ra, 1e-12)) + 2.0


def spectrum_frames(rate, mono, window_s=0.25, hop_s=0.02):
    nper = int(window_s * rate)
    hop = int(hop_s * rate)
    freqs, times, z = stft(mono, fs=rate, window="hann", nperseg=nper, noverlap=nper - hop, nfft=1 << 15, boundary=None, padded=False)
    # scipy scales by the window sum, doubling gives a sine's peak amplitude
    magnitude = np.abs(z) * 2.0
    return freqs, times, magnitude


def to_db(x):
    return 20.0 * np.log10(np.maximum(x, 1e-7))


def key_orders(cylinders):
    firing = cylinders / 2.0
    orders = {0.5, 1.0, firing, 2.0 * firing, 3.0 * firing}
    if cylinders >= 6:
        orders.add(firing / 2.0)
    return sorted(o for o in orders if o >= 0.5)


def analyze_run(base, out_dir):
    rate, mono, data, meta = load_run(base)
    name = Path(base).name
    cylinders = meta["cylinders"]
    firing = cylinders / 2.0
    freqs, times, magnitude = spectrum_frames(rate, mono)
    rpm_heard = np.interp(times, data["time"], data["rpm_heard"])
    throttle = np.interp(times, data["time"], data["throttle"])
    db = to_db(magnitude)

    # spectrogram with the firing frequency and its first harmonics on top
    fig, ax = plt.subplots(figsize=(13, 5.5))
    band = (freqs >= 20) & (freqs <= 12000)
    peak_db = db[band].max()
    mesh = ax.pcolormesh(times, freqs[band], db[band], shading="auto", cmap="magma", vmin=peak_db - 70, vmax=peak_db)
    for k, style in ((1, "--"), (2, ":")):
        ax.plot(times, rpm_heard / 60.0 * firing * k, "c" + style, linewidth=0.8, alpha=0.8, label=f"{k}x firing")
    ax.plot(times, rpm_heard / 60.0 * 0.5, "w:", linewidth=0.8, alpha=0.6, label="order 0.5")
    ax.set_yscale("log")
    ax.set_ylim(20, 12000)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("Hz")
    ax.set_title(f"{meta['car']} / {meta['scenario']} spectrogram (dB, 70 dB range)")
    ax.legend(loc="upper left", fontsize=8)
    fig.colorbar(mesh, ax=ax)
    fig.tight_layout()
    fig.savefig(out_dir / f"{name}_spectrogram.png", dpi=110)
    plt.close(fig)

    metrics = {"car": meta["car"], "scenario": meta["scenario"]}
    peak = float(np.abs(mono).max())
    rms = float(np.sqrt(np.mean(mono**2)))
    metrics["peak"] = peak
    metrics["rms_db"] = float(to_db(rms))
    metrics["crest_db"] = float(to_db(peak / max(rms, 1e-9)))
    metrics["min_limiter_gain"] = float(data["limiter_gain"].min())
    metrics["pops_fired"] = int(data["pops_fired"].max())

    if meta["scenario"] in sweep_scenarios:
        metrics.update(analyze_sweep(rate, mono, freqs, times, magnitude, rpm_heard, throttle, meta, name, out_dir, data))

    plot_cycles(rate, mono, data, meta, name, out_dir)
    return metrics


def crank_domain_orders(rate, mono, data, revs_per_block=8, samples_per_rev=1024, cutoff_hz=5000.0):
    """computed order tracking: resample onto a uniform crank angle grid, then every fft bin is an exact
    engine order no matter how fast the sweep is, which a fixed time window cannot give at low rpm"""
    t = np.arange(len(mono)) / rate
    rpm = np.interp(t, data["time"], data["rpm_heard"])
    revs = np.cumsum(rpm / 60.0) / rate
    sos = butter(8, cutoff_hz, fs=rate, output="sos")
    filtered = sosfiltfilt(sos, mono)
    grid = np.arange(0.0, revs[-1], 1.0 / samples_per_rev)
    angular = np.interp(grid, revs, filtered)
    grid_rpm = np.interp(grid, revs, rpm)
    grid_time = np.interp(grid, revs, t)
    block = revs_per_block * samples_per_rev
    hop = samples_per_rev
    window = np.hanning(block)
    gain = 2.0 / window.sum()
    starts = np.arange(0, len(angular) - block, hop)
    spectra = np.empty((block // 2 + 1, len(starts)))
    for j, s in enumerate(starts):
        spectra[:, j] = np.abs(np.fft.rfft(angular[s:s + block] * window)) * gain
    orders = np.fft.rfftfreq(block, d=1.0 / samples_per_rev)
    centers = starts + block // 2
    return orders, spectra, grid_rpm[centers], grid_time[centers]


def analyze_sweep(rate, mono, freqs, times, magnitude, rpm_heard, throttle, meta, name, out_dir, data):
    cylinders = meta["cylinders"]
    firing = cylinders / 2.0
    orders_all, spectra, block_rpm, block_time = crank_domain_orders(rate, mono, data)
    block_throttle = np.interp(block_time, data["time"], data["throttle"])
    # only blocks of the rising sweep under throttle
    rising = np.concatenate([[False], np.diff(block_rpm) > 0]) & (block_throttle > 0.2)
    sel = np.where(rising)[0]
    # a live dyno wobbles a little, plot against rpm in order
    sel = sel[np.argsort(block_rpm[sel], kind="stable")]
    if len(sel) < 8:
        return {}
    rpm = block_rpm[sel]

    max_order = 3.5 * firing
    keep = (orders_all >= 0.25) & (orders_all <= max_order)
    orders = orders_all[keep]
    order_map = spectra[keep][:, sel]
    order_db = to_db(order_map)

    # broadband measures come from the time domain spectrum at the same moments
    frame_index = np.clip(np.searchsorted(times, block_time[sel]), 0, len(times) - 1)
    frames = magnitude[:, frame_index]
    weights = 10.0 ** (a_weighting_db(freqs) / 20.0)
    power = (frames**2).sum(axis=0)
    level_db = 10.0 * np.log10(np.maximum(power, 1e-14))
    level_a_db = 10.0 * np.log10(np.maximum(((frames * weights[:, None]) ** 2).sum(axis=0), 1e-14))
    centroid = (frames * freqs[:, None]).sum(axis=0) / np.maximum(frames.sum(axis=0), 1e-12)

    # energy off the firing harmonics (half orders, cylinder to cylinder differences), the burble
    # only the half order lines count, the bins between them hold window leakage, not engine content
    half_orders = np.abs(orders * 2.0 - np.round(orders * 2.0)) < 1e-6
    firing_harmonics = np.abs(orders / firing - np.round(orders / firing)) < 1e-6
    on = (order_map[firing_harmonics] ** 2).sum(axis=0)
    off = (order_map[half_orders & ~firing_harmonics] ** 2).sum(axis=0)
    irregularity_db = 10.0 * np.log10(np.maximum(off, 1e-14) / np.maximum(on, 1e-14))

    tracked = key_orders(cylinders)
    order_lines = {o: to_db(order_map[np.argmin(np.abs(orders - o))]) for o in tracked}

    fig, axes = plt.subplots(3, 1, figsize=(13, 13), sharex=True)
    ax = axes[0]
    top = order_db.max()
    mesh = ax.pcolormesh(rpm, orders, order_db, shading="auto", cmap="magma", vmin=top - 60, vmax=top)
    for k in range(1, 4):
        ax.axhline(firing * k, color="c", linewidth=0.6, linestyle="--", alpha=0.6)
    ax.set_ylabel("engine order (per crank rev)")
    ax.set_title(f"{meta['car']} / {meta['scenario']} order map, dashed = firing order harmonics")
    fig.colorbar(mesh, ax=ax)

    ax = axes[1]
    for o, line in order_lines.items():
        ax.plot(rpm, line, label=f"order {o:g}", linewidth=1.3 if abs(o - firing) < 1e-3 else 0.9)
    ax.set_ylabel("order amplitude (dBFS)")
    ax.legend(ncol=4, fontsize=8)
    ax.grid(alpha=0.3)

    ax = axes[2]
    ax.plot(rpm, level_db, label="overall level (dB)")
    ax.plot(rpm, level_a_db, label="A-weighted level (dB)")
    ax.plot(rpm, irregularity_db, label="off-firing / firing energy (dB)")
    ax.set_ylabel("dB")
    ax.set_xlabel("rpm")
    ax.grid(alpha=0.3)
    twin = ax.twinx()
    twin.plot(rpm, centroid, "k--", linewidth=0.9, label="spectral centroid (Hz)")
    twin.set_ylabel("centroid Hz")
    lines = ax.get_legend_handles_labels()
    lines2 = twin.get_legend_handles_labels()
    ax.legend(lines[0] + lines2[0], lines[1] + lines2[1], fontsize=8, loc="lower right")
    fig.tight_layout()
    fig.savefig(out_dir / f"{name}_orders.png", dpi=100)
    plt.close(fig)

    grid = np.arange(1000.0, meta["redline_rpm"] + 1.0, 500.0)
    grid = grid[(grid >= rpm.min()) & (grid <= rpm.max())]

    def sample(values):
        order = np.argsort(rpm)
        return [round(float(v), 2) for v in np.interp(grid, rpm[order], values[order])]

    # each layer's own level before the mix, so a quiet exhaust is not mistaken for a bright one
    layers = {}
    for layer in ("exhaust", "intake", "turbo", "mechanical"):
        column = f"{layer}_rms"
        if column in data.dtype.names:
            layers[layer] = sample(to_db(np.interp(block_time[sel], data["time"], data[column])))

    return {
        "rpm_grid": [float(g) for g in grid],
        "layers_db": layers,
        "level_db": sample(level_db),
        "level_a_db": sample(level_a_db),
        "centroid_hz": sample(centroid),
        "irregularity_db": sample(irregularity_db),
        "orders_db": {f"{o:g}": sample(line) for o, line in order_lines.items()},
        "series": {
            "rpm": rpm.tolist(),
            "level_db": level_db.tolist(),
            "level_a_db": level_a_db.tolist(),
            "centroid_hz": centroid.tolist(),
            "irregularity_db": irregularity_db.tolist(),
            "firing_order_db": order_lines[min(order_lines, key=lambda o: abs(o - firing))].tolist(),
        },
    }


def plot_cycles(rate, mono, data, meta, name, out_dir):
    """two full engine cycles (720 degrees each) at a few moments, the pressure pulse train itself"""
    duration = len(mono) / rate
    moments = [0.2, 0.5, 0.92] if meta["scenario"] in sweep_scenarios else [0.25, 0.5, 0.85]
    fig, axes = plt.subplots(len(moments), 1, figsize=(13, 2.6 * len(moments)))
    for ax, m in zip(axes, moments):
        t0 = m * duration
        rpm = float(np.interp(t0, data["time"], data["rpm_heard"]))
        cycle = 120.0 / max(rpm, 100.0)
        a = int(t0 * rate)
        b = min(len(mono), a + int(2.0 * cycle * rate))
        t = (np.arange(a, b) / rate - t0) / cycle * 720.0
        ax.plot(t, mono[a:b], linewidth=0.7)
        spacing = 720.0 / meta["cylinders"]
        for k in range(int(1440 / spacing) + 1):
            ax.axvline(k * spacing, color="grey", linewidth=0.4, alpha=0.5)
        ax.set_xlim(0, 1440)
        ax.set_title(f"{rpm:.0f} rpm at {t0:.2f} s, grid = even firing interval", fontsize=9)
        ax.set_ylabel("amplitude")
    axes[-1].set_xlabel("crank degrees (two cycles)")
    fig.suptitle(f"{meta['car']} / {meta['scenario']} waveform over engine cycles")
    fig.tight_layout()
    fig.savefig(out_dir / f"{name}_cycles.png", dpi=100)
    plt.close(fig)


def command_render(args):
    exe = root / "bin" / f"{args.exe}.exe"
    if not exe.exists():
        subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-Command", f"& '{root / 'build.ps1'}' -Name {args.exe}"], check=True)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    cars = sorted(cars_dir.glob("*.car"))
    if args.cars:
        wanted = args.cars.split(",")
        cars = [c for c in cars if c.stem in wanted]
    scenarios = args.scenarios.split(",") if args.scenarios else all_scenarios
    for car in cars:
        for scenario in scenarios:
            base = out_dir / f"{car.stem}_{scenario}"
            command = [str(exe), f"--car={car}", f"--scenario={scenario}", f"--out={base}", f"--view={args.view}"]
            for stage in ("engine_stage", "exhaust_stage", "intake_stage", "turbo_stage", "muffler"):
                value = getattr(args, stage)
                if value is not None:
                    command.append(f"--{stage}={value}")
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    command_analyze(argparse.Namespace(dir=str(out_dir)))


def command_analyze(args):
    out_dir = Path(args.dir)
    summary = {}
    for wav in sorted(out_dir.glob("*.wav")):
        base = wav.with_suffix("")
        if not Path(str(base) + ".csv").exists():
            continue
        metrics = analyze_run(base, out_dir)
        summary[base.name] = metrics
        brief = {k: v for k, v in metrics.items() if k not in ("series", "orders_db", "rpm_grid", "level_db", "level_a_db", "centroid_hz", "irregularity_db", "layers_db")}
        print(base.name, json.dumps(brief))
    (out_dir / "metrics.json").write_text(json.dumps(summary, indent=1))


def command_compare(args):
    before = json.loads((Path(args.before) / "metrics.json").read_text())
    after = json.loads((Path(args.after) / "metrics.json").read_text())
    out_dir = Path(args.out or args.after)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name in sorted(set(before) & set(after)):
        a, b = before[name], after[name]
        print(f"{name}: rms {a['rms_db']:.1f} -> {b['rms_db']:.1f} dB, crest {a['crest_db']:.1f} -> {b['crest_db']:.1f} dB, pops {a['pops_fired']} -> {b['pops_fired']}")
        if "series" not in a or "series" not in b:
            continue
        fig, axes = plt.subplots(4, 1, figsize=(12, 12), sharex=True)
        for ax, key, label in zip(
            axes,
            ("level_a_db", "firing_order_db", "irregularity_db", "centroid_hz"),
            ("A-weighted level (dB)", "firing order amplitude (dB)", "off-firing / firing energy (dB)", "spectral centroid (Hz)"),
        ):
            ax.plot(a["series"]["rpm"], a["series"][key], label="before", alpha=0.8)
            ax.plot(b["series"]["rpm"], b["series"][key], label="after", alpha=0.8)
            ax.set_ylabel(label)
            ax.grid(alpha=0.3)
            ax.legend(fontsize=8)
        axes[-1].set_xlabel("rpm")
        fig.suptitle(f"{name}: before vs after")
        fig.tight_layout()
        fig.savefig(out_dir / f"compare_{name}.png", dpi=100)
        plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    render = sub.add_parser("render")
    render.add_argument("--out", default=str(root / "out" / "latest"))
    render.add_argument("--cars", default="")
    render.add_argument("--scenarios", default="")
    render.add_argument("--view", default="chase", choices=["chase", "hood", "cabin"])
    render.add_argument("--exe", default="engine_sound_render", help="renderer in bin/, build.ps1 -Name makes others")
    for stage in ("engine_stage", "exhaust_stage", "intake_stage", "turbo_stage", "muffler"):
        render.add_argument(f"--{stage}", type=float, default=None)
    render.set_defaults(func=command_render)
    analyze = sub.add_parser("analyze")
    analyze.add_argument("dir")
    analyze.set_defaults(func=command_analyze)
    compare = sub.add_parser("compare")
    compare.add_argument("before")
    compare.add_argument("after")
    compare.add_argument("--out", default="")
    compare.set_defaults(func=command_compare)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    sys.exit(main())
