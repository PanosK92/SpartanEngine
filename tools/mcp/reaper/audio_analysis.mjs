import node_fs from "node:fs";
import zlib from "node:zlib";

// wav decoding

export function read_wav(path)
{
    const buffer = node_fs.readFileSync(path);
    if (buffer.toString("ascii", 0, 4) !== "RIFF" || buffer.toString("ascii", 8, 12) !== "WAVE")
    {
        throw new Error(`${path} is not a RIFF/WAVE file`);
    }

    let offset = 12;
    let format = null;
    let data = null;
    while (offset + 8 <= buffer.length)
    {
        const id = buffer.toString("ascii", offset, offset + 4);
        const size = buffer.readUInt32LE(offset + 4);
        const body = offset + 8;
        if (id === "fmt ")
        {
            let tag = buffer.readUInt16LE(body);
            if (tag === 0xFFFE)
            {
                tag = buffer.readUInt16LE(body + 24);
            }
            format = { tag, channels: buffer.readUInt16LE(body + 2), sample_rate: buffer.readUInt32LE(body + 4), bits: buffer.readUInt16LE(body + 14) };
        }
        else if (id === "data")
        {
            data = { start: body, size: Math.min(size, buffer.length - body) };
        }
        offset = body + size + (size & 1);
    }
    if (!format || !data)
    {
        throw new Error(`${path} is missing fmt or data chunk`);
    }

    const bytes_per_sample = format.bits / 8;
    const frame_count = Math.floor(data.size / (bytes_per_sample * format.channels));
    const channels = Array.from({ length: format.channels }, () => new Float32Array(frame_count));
    let cursor = data.start;
    for (let i = 0; i < frame_count; i++)
    {
        for (let c = 0; c < format.channels; c++)
        {
            let value;
            if (format.tag === 3 && format.bits === 32)
            {
                value = buffer.readFloatLE(cursor);
            }
            else if (format.tag === 3 && format.bits === 64)
            {
                value = buffer.readDoubleLE(cursor);
            }
            else if (format.bits === 16)
            {
                value = buffer.readInt16LE(cursor) / 32768;
            }
            else if (format.bits === 24)
            {
                value = buffer.readIntLE(cursor, 3) / 8388608;
            }
            else if (format.bits === 32)
            {
                value = buffer.readInt32LE(cursor) / 2147483648;
            }
            else if (format.bits === 8)
            {
                value = (buffer.readUInt8(cursor) - 128) / 128;
            }
            else
            {
                throw new Error(`unsupported wav format tag ${format.tag} / ${format.bits} bits`);
            }
            channels[c][i] = value;
            cursor += bytes_per_sample;
        }
    }
    return { sample_rate: format.sample_rate, channels, frame_count };
}

// loudness (ITU-R BS.1770 K-weighting with gating)

function biquad_coefficients(type, gain_db, q, fc, fs)
{
    const a = Math.pow(10, gain_db / 40);
    const w0 = 2 * Math.PI * fc / fs;
    const alpha = Math.sin(w0) / (2 * q);
    const cos_w0 = Math.cos(w0);
    let b0, b1, b2, a0, a1, a2;
    if (type === "high_shelf")
    {
        const sqrt_a = Math.sqrt(a);
        b0 = a * ((a + 1) + (a - 1) * cos_w0 + 2 * sqrt_a * alpha);
        b1 = -2 * a * ((a - 1) + (a + 1) * cos_w0);
        b2 = a * ((a + 1) + (a - 1) * cos_w0 - 2 * sqrt_a * alpha);
        a0 = (a + 1) - (a - 1) * cos_w0 + 2 * sqrt_a * alpha;
        a1 = 2 * ((a - 1) - (a + 1) * cos_w0);
        a2 = (a + 1) - (a - 1) * cos_w0 - 2 * sqrt_a * alpha;
    }
    else
    {
        b0 = (1 + cos_w0) / 2;
        b1 = -(1 + cos_w0);
        b2 = (1 + cos_w0) / 2;
        a0 = 1 + alpha;
        a1 = -2 * cos_w0;
        a2 = 1 - alpha;
    }
    return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
}

function apply_biquad(input, [b0, b1, b2, a1, a2])
{
    const output = new Float32Array(input.length);
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < input.length; i++)
    {
        const x = input[i];
        const y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        output[i] = y;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
    }
    return output;
}

function k_weight(channel, fs)
{
    const shelf = biquad_coefficients("high_shelf", 4.0, 1 / Math.sqrt(2), 1500, fs);
    const high_pass = biquad_coefficients("high_pass", 0, 0.5, 38, fs);
    return apply_biquad(apply_biquad(channel, shelf), high_pass);
}

function block_loudness(weighted, start, length)
{
    let sum = 0;
    for (const channel of weighted)
    {
        let acc = 0;
        const end = Math.min(start + length, channel.length);
        for (let i = start; i < end; i++)
        {
            acc += channel[i] * channel[i];
        }
        sum += acc / Math.max(1, end - start);
    }
    return sum;
}

function to_lufs(power)
{
    return power > 0 ? -0.691 + 10 * Math.log10(power) : -Infinity;
}

function integrated_loudness(weighted, fs, frame_count)
{
    const block = Math.round(0.4 * fs);
    const hop = Math.round(0.1 * fs);
    const powers = [];
    for (let start = 0; start + block <= frame_count; start += hop)
    {
        powers.push(block_loudness(weighted, start, block));
    }
    const absolute = powers.filter((p) => to_lufs(p) > -70);
    if (absolute.length === 0)
    {
        return -Infinity;
    }
    const relative_gate = to_lufs(absolute.reduce((a, b) => a + b, 0) / absolute.length) - 10;
    const gated = absolute.filter((p) => to_lufs(p) > relative_gate);
    return to_lufs(gated.reduce((a, b) => a + b, 0) / Math.max(1, gated.length));
}

// fft

function fft_in_place(re, im)
{
    const n = re.length;
    for (let i = 1, j = 0; i < n; i++)
    {
        let bit = n >> 1;
        for (; j & bit; bit >>= 1)
        {
            j ^= bit;
        }
        j ^= bit;
        if (i < j)
        {
            [re[i], re[j]] = [re[j], re[i]];
            [im[i], im[j]] = [im[j], im[i]];
        }
    }
    for (let len = 2; len <= n; len <<= 1)
    {
        const angle = -2 * Math.PI / len;
        const w_re = Math.cos(angle);
        const w_im = Math.sin(angle);
        for (let i = 0; i < n; i += len)
        {
            let cur_re = 1, cur_im = 0;
            for (let k = 0; k < len / 2; k++)
            {
                const a = i + k;
                const b = a + len / 2;
                const t_re = re[b] * cur_re - im[b] * cur_im;
                const t_im = re[b] * cur_im + im[b] * cur_re;
                re[b] = re[a] - t_re;
                im[b] = im[a] - t_im;
                re[a] += t_re;
                im[a] += t_im;
                const next_re = cur_re * w_re - cur_im * w_im;
                cur_im = cur_re * w_im + cur_im * w_re;
                cur_re = next_re;
            }
        }
    }
}

// png

const crc_table = (() =>
{
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++)
    {
        let c = n;
        for (let k = 0; k < 8; k++)
        {
            c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
        }
        table[n] = c >>> 0;
    }
    return table;
})();

function crc32(buffer)
{
    let c = 0xFFFFFFFF;
    for (const byte of buffer)
    {
        c = crc_table[(c ^ byte) & 0xFF] ^ (c >>> 8);
    }
    return (c ^ 0xFFFFFFFF) >>> 0;
}

function png_chunk(type, data)
{
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, "ascii"), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(body));
    return Buffer.concat([length, body, crc]);
}

function encode_png(width, height, rgb)
{
    const raw = Buffer.alloc((width * 3 + 1) * height);
    for (let y = 0; y < height; y++)
    {
        raw[y * (width * 3 + 1)] = 0;
        rgb.copy(raw, y * (width * 3 + 1) + 1, y * width * 3, (y + 1) * width * 3);
    }
    const header = Buffer.alloc(13);
    header.writeUInt32BE(width, 0);
    header.writeUInt32BE(height, 4);
    header[8] = 8;
    header[9] = 2;
    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
        png_chunk("IHDR", header),
        png_chunk("IDAT", zlib.deflateSync(raw)),
        png_chunk("IEND", Buffer.alloc(0)),
    ]);
}

const palette_stops = [[0, 0, 4], [40, 11, 84], [101, 21, 110], [159, 42, 99], [212, 72, 66], [245, 125, 21], [250, 193, 39], [252, 255, 164]];

function palette(t)
{
    const x = Math.min(1, Math.max(0, t)) * (palette_stops.length - 1);
    const i = Math.min(palette_stops.length - 2, Math.floor(x));
    const f = x - i;
    return palette_stops[i].map((v, k) => Math.round(v + (palette_stops[i + 1][k] - v) * f));
}

// analysis entry point

const bands = [["sub", 20, 60], ["low", 60, 250], ["low_mid", 250, 1000], ["mid", 1000, 4000], ["high", 4000, 12000], ["air", 12000, 20000]];

export function analyze_wav(path, options = {})
{
    const { sample_rate: fs, channels, frame_count } = read_wav(path);
    const seconds = frame_count / fs;

    let peak = 0;
    let clipped = 0;
    let sum_sq = 0;
    for (const channel of channels)
    {
        for (let i = 0; i < channel.length; i++)
        {
            const v = Math.abs(channel[i]);
            peak = Math.max(peak, v);
            if (v >= 0.999)
            {
                clipped++;
            }
            sum_sq += channel[i] * channel[i];
        }
    }
    const rms = Math.sqrt(sum_sq / Math.max(1, frame_count * channels.length));
    const db = (v) => (v > 0 ? 20 * Math.log10(v) : -Infinity);

    const weighted = channels.slice(0, 2).map((channel) => k_weight(channel, fs));
    const window_seconds = options.timeline_seconds ?? 1;
    const window = Math.max(1, Math.round(window_seconds * fs));
    const timeline = [];
    for (let start = 0; start < frame_count; start += window)
    {
        let window_peak = 0;
        for (const channel of channels)
        {
            const end = Math.min(start + window, frame_count);
            for (let i = start; i < end; i++)
            {
                window_peak = Math.max(window_peak, Math.abs(channel[i]));
            }
        }
        timeline.push({ t: +(start / fs).toFixed(2), lufs: +to_lufs(block_loudness(weighted, start, window)).toFixed(1), peak_db: +db(window_peak).toFixed(1) });
    }

    const mono = new Float32Array(frame_count);
    for (const channel of channels)
    {
        for (let i = 0; i < frame_count; i++)
        {
            mono[i] += channel[i] / channels.length;
        }
    }

    let correlation = null;
    if (channels.length >= 2)
    {
        let lr = 0, ll = 0, rr = 0;
        for (let i = 0; i < frame_count; i++)
        {
            lr += channels[0][i] * channels[1][i];
            ll += channels[0][i] * channels[0][i];
            rr += channels[1][i] * channels[1][i];
        }
        correlation = ll > 0 && rr > 0 ? +(lr / Math.sqrt(ll * rr)).toFixed(3) : null;
    }

    const fft_size = 4096;
    const width = Math.min(options.width ?? 1200, Math.max(1, Math.floor(frame_count / 512)));
    const height = options.height ?? 360;
    const hop = Math.max(1, Math.floor((frame_count - fft_size) / Math.max(1, width - 1)));
    const hann = Float32Array.from({ length: fft_size }, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (fft_size - 1)));
    const band_energy = new Float64Array(bands.length);
    const f_min = 30, f_max = Math.min(20000, fs / 2);
    const row_bins = Array.from({ length: height }, (_, y) =>
    {
        const f = f_min * Math.pow(f_max / f_min, 1 - y / (height - 1));
        return Math.min(fft_size / 2 - 1, Math.round(f * fft_size / fs));
    });
    const rgb = Buffer.alloc(width * height * 3);
    const re = new Float64Array(fft_size);
    const im = new Float64Array(fft_size);
    for (let x = 0; x < width; x++)
    {
        const start = x * hop;
        for (let i = 0; i < fft_size; i++)
        {
            re[i] = (mono[start + i] ?? 0) * hann[i];
            im[i] = 0;
        }
        fft_in_place(re, im);
        const magnitude = new Float64Array(fft_size / 2);
        let column_power = 0;
        let column_weighted = 0;
        for (let k = 0; k < fft_size / 2; k++)
        {
            const power = re[k] * re[k] + im[k] * im[k];
            magnitude[k] = power;
            const f = k * fs / fft_size;
            column_power += power;
            column_weighted += power * f;
            for (let b = 0; b < bands.length; b++)
            {
                if (f >= bands[b][1] && f < bands[b][2])
                {
                    band_energy[b] += power;
                }
            }
        }
        const window_index = Math.floor((start + fft_size / 2) / window);
        if (timeline[window_index])
        {
            timeline[window_index].power = (timeline[window_index].power ?? 0) + column_power;
            timeline[window_index].weighted = (timeline[window_index].weighted ?? 0) + column_weighted;
        }
        for (let y = 0; y < height; y++)
        {
            const level = 10 * Math.log10(magnitude[row_bins[y]] / (fft_size * fft_size / 16) + 1e-12);
            const [r, g, b] = palette((level + 100) / 100);
            const o = (y * width + x) * 3;
            rgb[o] = r;
            rgb[o + 1] = g;
            rgb[o + 2] = b;
        }
    }

    for (const entry of timeline)
    {
        entry.centroid_hz = entry.power > 0 ? Math.round(entry.weighted / entry.power) : null;
        delete entry.power;
        delete entry.weighted;
    }

    const total_band = band_energy.reduce((a, b) => a + b, 0) || 1;
    const band_balance = Object.fromEntries(bands.map(([name], b) => [name, +(10 * Math.log10(band_energy[b] / total_band + 1e-12)).toFixed(1)]));

    let spectrogram_path = null;
    if (options.spectrogram !== false)
    {
        spectrogram_path = path.replace(/\.wav$/i, "") + "_spectrogram.png";
        node_fs.writeFileSync(spectrogram_path, encode_png(width, height, rgb));
    }

    return {
        path,
        seconds: +seconds.toFixed(2),
        sample_rate: fs,
        channels: channels.length,
        peak_dbfs: +db(peak).toFixed(2),
        rms_dbfs: +db(rms).toFixed(2),
        integrated_lufs: +integrated_loudness(weighted, fs, frame_count).toFixed(1),
        crest_db: +(db(peak) - db(rms)).toFixed(1),
        clipped_samples: clipped,
        stereo_correlation: correlation,
        band_balance_db: band_balance,
        timeline,
        spectrogram_path,
        spectrogram_axes: spectrogram_path ? `x: time 0-${seconds.toFixed(1)}s, y: log frequency ${f_min}Hz (bottom) to ${f_max}Hz (top), color: -100 dB (black) to 0 dB (pale yellow)` : null,
    };
}
