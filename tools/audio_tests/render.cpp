// Offline DSP regression and listening fixture; uses the production synthesizer.
#include "CarEngineSoundSynthesis.h"
#include "CarTireSquealSynthesis.h"
#include "../../source/io/pugixml.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace engine_sound;
constexpr int rate = 48000;
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

engine_config read_config(const std::filesystem::path& path)
{
    pugi::xml_document document;
    check(document.load_file(path.string().c_str()), "parse car XML");
    auto e = document.child("car").child("engine");
    engine_config c;
#define F(member, attr) c.member = e.attribute(attr).as_float(c.member)
    c.cylinder_count = e.attribute("engine_sound_cylinders").as_int(c.cylinder_count);
    c.bank_count = e.attribute("engine_sound_banks").as_int(c.bank_count);
    F(bank_angle_deg, "engine_bank_angle_deg");
    F(idle_rpm, "engine_idle_rpm"); F(redline_rpm, "engine_redline_rpm"); F(max_rpm, "engine_max_rpm");
    F(displacement_l, "engine_displacement_l"); F(bore_mm, "engine_bore_mm"); F(stroke_mm, "engine_stroke_mm");
    F(compression_ratio, "engine_compression_ratio"); F(primary_length_m, "exhaust_primary_length_m");
    F(collector_length_m, "exhaust_collector_length_m"); F(tailpipe_length_m, "exhaust_tailpipe_length_m");
    F(muffler_level, "exhaust_muffler_level"); F(boost_max_pressure, "boost_max_pressure");
    F(boost_wastegate_rpm, "boost_wastegate_rpm");
#ifndef AUDIO_BASELINE
    F(intake_runner_length_m, "intake_runner_length_m"); F(intake_valve_duration_deg, "intake_valve_duration_deg");
    F(combustion_variation, "engine_combustion_variation"); F(crank_inertia, "engine_inertia");
    c.turbo_bypass_valve = e.attribute("turbo_bypass_valve").as_bool(c.turbo_bypass_valve);
#endif
#undef F
    c.turbo_enabled = e.attribute("turbo_enabled").as_bool();
    auto array = [&](const char* name, auto* values)
    {
        std::string text = e.attribute(name).as_string();
        std::replace(text.begin(), text.end(), ',', ' ');
        std::istringstream stream(text);
        for (int i = 0; i < c.cylinder_count; i++) if (!(stream >> values[i])) break;
    };
    array("engine_firing_order", c.firing_order);
    array("engine_cylinder_bank", c.cylinder_bank);
#ifndef AUDIO_BASELINE
    array("engine_firing_intervals_deg", c.firing_intervals_deg);
#endif
    return c;
}

std::vector<float> steady(engine_config c, int block, bool stereo = true, int sample_rate = rate)
{
    synthesizer s;
    s.initialize(sample_rate); s.configure(c);
    s.set_parameters(4200, .75f, .7f, c.boost_max_pressure * .5f, false, 3, false, listener_view::chase);
    std::vector<float> result(static_cast<size_t>(sample_rate) * (stereo ? 2 : 1));
    for (int offset = 0; offset < sample_rate; offset += block)
        s.generate(result.data() + offset * (stereo ? 2 : 1), std::min(block, sample_rate - offset), stereo);
    return result;
}

double rms(const std::vector<float>& samples, size_t start = 0)
{
    double sum = 0;
    for (size_t i = start; i < samples.size(); i++) sum += samples[i] * samples[i];
    return sqrt(sum / static_cast<double>(samples.size() - start));
}

void regressions()
{
    engine_config c;
    auto a = steady(c, 127), b = steady(c, 1024), mono = steady(c, 239, false);
    check(a == b, "render must not depend on callback block size");
    for (size_t i = 0; i < mono.size(); i++)
        check(fabsf(mono[i] - .5f * (a[i * 2] + a[i * 2 + 1])) < 1e-6f, "mono/stereo consistency");
    synthesizer s; s.initialize(); s.configure(c);
    std::vector<float> buffer(rate * 2);
    s.set_parameters(0, 1, 1, 0, false, 1, false, listener_view::chase);
    s.generate(buffer.data(), rate);
    check(rms(buffer) == 0, "stopped engine must be silent");
    s.set_parameters(4200, .8f, .8f, 0, false, 3, false, listener_view::chase);
    s.generate(buffer.data(), rate);
    s.reset();
    s.generate(buffer.data(), rate);
    auto reset_render = buffer;
    s.reset(); s.generate(buffer.data(), rate);
    check(buffer == reset_render, "reset must reproduce the same waveform");
    s.set_parameters(0, 0, 0, 0, false, 1, false, listener_view::chase);
    s.generate(buffer.data(), rate);
    check(rms(buffer, rate) < 1e-7, "engine shutdown must settle to silence");
    s.generate(nullptr, 0); s.generate(nullptr, -1);
    for (int sr : { 44100, 48000, 96000 })
    {
        auto samples = steady(c, 251, true, sr);
        check(rms(samples) > .005, "sample rate must produce audible output");
        for (float x : samples) check(std::isfinite(x) && fabsf(x) < .98f, "finite, bounded output");
    }
    auto different = c; different.primary_length_m *= 1.6f;
    check(steady(different, 127) != a, "pipe geometry must change sound");
#ifndef AUDIO_BASELINE
    different = c; different.intake_runner_length_m = .6f;
    check(steady(different, 127) != a, "intake geometry must change sound");
    different = c; different.bank_count = 2; different.cylinder_count = 6; different.bank_angle_deg = 90;
    for (int i = 0; i < 6; i++) different.cylinder_bank[i] = i & 1;
    s.configure(different); s.reset(); s.generate(buffer.data(), rate);
    check(!s.get_debug().odd_fire, "bank angle must not invent odd firing");
    for (int i = 0; i < 6; i++) different.firing_intervals_deg[i] = (i & 1) ? 150.0f : 90.0f;
    s.configure(different); s.reset(); s.generate(buffer.data(), rate);
    check(s.get_debug().odd_fire, "explicit odd-fire crank intervals");
    s.set_parameters(std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, false, 1, false, listener_view::chase);
    s.generate(buffer.data(), rate);
    for (float x : buffer) check(std::isfinite(x), "nonfinite telemetry must not poison DSP");
    // An upgrade while a pipe is ringing must install after its short fade.
    s.set_parameters(6000, 1, 1, 0, false, 3, false, listener_view::chase);
    s.generate(buffer.data(), rate);
    s.configure(c);
    for (int i = 0; i < 30; i++) s.generate(buffer.data(), 256);
    check(s.get_config() == c, "live reconfiguration must finish");
    s.params.pop_rate = 0;
    s.set_parameters(7000, 1, 1, 0, true, 3, false, listener_view::chase);
    int pops = s.get_debug().pops_fired;
    s.generate(buffer.data(), rate);
    check(s.get_debug().pops_fired == pops, "zero pop rate disables limiter afterfire too");
    for (int count : { 1, 2, 3, 4, 6, 8, 10, 12, 16 })
    {
        auto extreme = c; extreme.cylinder_count = count; extreme.bank_count = 2;
        extreme.muffler_level = 0; extreme.redline_rpm = 20000; extreme.max_rpm = 22000;
        for (int i = 0; i < count; i++) extreme.cylinder_bank[i] = i & 1;
        s.configure(extreme); s.reset();
        s.params.master_gain = 2; s.params.exhaust_level = 3; s.params.intake_level = 4;
        s.set_parameters(22000, 1, 1, 0, false, 8, false, listener_view::hood);
        s.generate(buffer.data(), rate);
        for (float x : buffer) check(std::isfinite(x) && fabsf(x) < .98f, "extreme engine and mix headroom");
    }
    auto turbo = c; turbo.turbo_enabled = true; turbo.boost_max_pressure = 1;
    s.configure(turbo); s.reset(); s.params = runtime_params();
    s.params.exhaust_level = s.params.intake_level = s.params.mechanical_level = 0;
    s.set_parameters(6000, 1, 1, 1, false, 3, false, listener_view::hood);
    s.generate(buffer.data(), rate);
    s.set_parameters(6000, 0, 0, 0, false, 3, false, listener_view::hood);
    s.generate(buffer.data(), rate / 10);
    check(s.get_debug().turbo_level > .003f, "smoothed throttle release must trigger bypass valve");
#endif
    std::puts("PASS: block independence, mono, reset, silence, rates, spec sensitivity, firing timing, finite samples");
}

std::vector<float> startup(engine_config c, int block, int sr = rate)
{
    synthesizer s; s.initialize(sr); s.configure(c);
    s.set_parameters(c.idle_rpm, .03f, .12f, 0, false, 1, false, listener_view::chase);
    s.start();
    std::vector<float> samples(sr * 4);
    for (int offset = 0; offset < sr * 2; offset += block)
        s.generate(samples.data() + offset * 2, std::min(block, sr * 2 - offset));
    check(fabsf(s.get_debug().rpm - c.idle_rpm) < 1, "startup must hand over to live RPM");
    for (float x : samples) check(std::isfinite(x) && fabsf(x) < .98f, "startup headroom");
    return samples;
}

std::vector<float> tire_fixture(int block, bool stereo = true, int sr = rate)
{
    tire_squeal_sound::synthesizer s; s.initialize(sr);
    std::vector<float> samples(sr * 4 * (stereo ? 2 : 1));
    for (int second = 0; second < 4; second++)
    {
        // Silence, gentle scrub, full slide, release.
        s.set_parameters(second == 1 ? .2f : second == 2 ? 1.0f : 0.0f, .5f);
        for (int offset = 0; offset < sr; offset += block)
            s.generate(samples.data() + (second * sr + offset) * (stereo ? 2 : 1), std::min(block, sr - offset), stereo);
    }
    return samples;
}

void effect_regressions()
{
    engine_config c;
    check(startup(c, 127) == startup(c, 1024), "startup callback independence");
    synthesizer s; s.initialize(); s.configure(c);
    s.set_parameters(c.idle_rpm, .03f, .12f, 0, false, 1, false, listener_view::chase);
    std::vector<float> a(rate * 4), b(rate * 4);
    s.start(); s.generate(a.data(), rate * 2);
    s.start(); s.generate(b.data(), rate * 2);
    check(a == b, "re-entry must restart the complete startup sequence");
    auto heavier = c; heavier.crank_inertia *= 2; heavier.compression_ratio += 2;
    check(startup(heavier, 127) != a, "startup responds to engine specs");
    auto tire = tire_fixture(127), chunks = tire_fixture(1024), mono = tire_fixture(239, false);
    check(tire == chunks, "tire callback independence");
    double scrub = 0, squeal = 0;
    for (int i = 0; i < rate * 4; i++)
    {
        float x = mono[i];
        check(x == tire[i * 2] && x == tire[i * 2 + 1], "tire mono/stereo consistency");
        check(std::isfinite(x) && fabsf(x) < .71f, "tire headroom");
        if (i < rate || i > rate * 7 / 2) check(x == 0, "no slip and released tires must be silent");
        if (i >= rate && i < rate * 2) scrub += x * x;
        if (i >= rate * 2 && i < rate * 3) squeal += x * x;
    }
    check(scrub > 0 && squeal > scrub * 4, "slip must grow from scrub into squeal");
    tire_squeal_sound::synthesizer t; t.initialize();
    t.set_parameters(1, 1); t.generate(a.data(), rate * 2);
    t.reset(); t.generate(a.data(), rate * 2);
    check(rms(a) == 0, "tire reset clears stale slip and resonances");
    t.set_parameters(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    t.generate(a.data(), rate * 2);
    check(rms(a) == 0, "invalid tire telemetry stays silent");
    t.generate(nullptr, 0); t.generate(nullptr, -1);
    for (int sr : { 8000, 44100, 96000, 192000 })
    {
        startup(c, 251, sr);
        for (float x : tire_fixture(251, true, sr))
            check(std::isfinite(x) && fabsf(x) < .71f, "tire supported sample rates");
    }
    std::puts("PASS: startup handover, restart, spec sensitivity; tire silence, slip response, reset, rates, headroom, block independence");
}

void save_tire_preview(const std::filesystem::path& path)
{
    auto samples = tire_fixture(256);
    FILE* file = nullptr;
    check(fopen_s(&file, path.string().c_str(), "wb") == 0, "open tire preview");
    const uint32_t bytes = static_cast<uint32_t>(samples.size() * 2), riff = 36 + bytes;
    const uint32_t fmt_size = 16, hz = rate, byte_rate = rate * 4;
    const uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, file); fwrite(&riff, 4, 1, file); fwrite("WAVEfmt ", 1, 8, file);
    fwrite(&fmt_size, 4, 1, file); fwrite(&pcm, 2, 1, file); fwrite(&channels, 2, 1, file);
    fwrite(&hz, 4, 1, file); fwrite(&byte_rate, 4, 1, file); fwrite(&align, 2, 1, file); fwrite(&bits, 2, 1, file);
    fwrite("data", 1, 4, file); fwrite(&bytes, 4, 1, file);
    for (float x : samples)
    {
        int16_t value = static_cast<int16_t>(std::clamp(x, -1.0f, 1.0f) * 32767);
        fwrite(&value, 2, 1, file);
    }
    check(fclose(file) == 0, "save tire preview");
}

int main(int argc, char** argv)
try
{
    std::filesystem::path output = argc > 1 ? argv[1] : "binaries/audio_tests/after";
    std::filesystem::create_directories(output);
#ifndef AUDIO_BASELINE
    regressions();
    effect_regressions();
    save_tire_preview(output / "tire_scrub_slide_release.wav");
#endif
    for (const auto& entry : std::filesystem::directory_iterator("worlds/cars"))
    {
        if (entry.path().extension() != ".car") continue;
        auto c = read_config(entry.path());
        synthesizer s; s.initialize(); s.configure(c);
        s.start();
        constexpr int seconds = 12, block = 256;
        check(s.begin_dump(seconds), "start WAV capture");
        std::vector<float> buffer(block * 2);
        double energy = 0, peak = 0, delta = 0;
        float previous[2] = {};
        auto start = std::chrono::steady_clock::now();
        for (int offset = 0; offset < seconds * rate; offset += block)
        {
            float t = static_cast<float>(offset) / rate;
            float rpm = c.idle_rpm, throttle = .03f, load = .12f;
            bool shift = false, cut = false;
            auto view = listener_view::chase;
            if (t >= 2 && t < 7)
            {
                float progress = (t - 2) / 5;
                rpm += (c.redline_rpm - rpm) * progress;
                throttle = 1; load = .9f;
                shift = t >= 4.5f && t < 4.6f;
                if (shift) { rpm *= .8f; load = .12f; }
            }
            else if (t >= 7 && t < 7.4f) { rpm = c.max_rpm; throttle = 1; load = .9f; cut = true; }
            else if (t >= 7.4f && t < 10) { rpm = c.redline_rpm * (1 - (t - 7.4f) / 4); throttle = 0; load = .08f; }
            else if (t >= 10 && t < 11) { rpm = 4000; throttle = .6f; load = .6f; view = listener_view::cabin; }
            else if (t >= 11) { rpm = 0; throttle = 0; load = 0; }
            float boost = c.boost_max_pressure * throttle * std::clamp(rpm / c.redline_rpm, 0.0f, 1.0f);
            s.set_parameters(rpm, throttle, load, boost, cut, 3, shift, view);
            int frames = std::min(block, seconds * rate - offset);
            s.generate(buffer.data(), frames);
            for (int i = 0; i < frames * 2; i++)
            {
                float x = buffer[i];
                check(std::isfinite(x) && fabsf(x) < .98f, "preview output must stay finite with headroom");
                energy += x * x; peak = std::max(peak, static_cast<double>(fabsf(x)));
                delta = std::max(delta, static_cast<double>(fabsf(x - previous[i & 1]))); previous[i & 1] = x;
            }
        }
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        auto path = output / (entry.path().stem().string() + ".wav");
        check(s.dump_ready() && s.save_dump(path.string().c_str()), "save completed WAV");
        std::printf("%s: RMS %.5f, peak %.5f, max step %.5f, %.1fx realtime, pops %d\n", path.filename().string().c_str(), sqrt(energy / (seconds * rate * 2)), peak, delta, seconds / elapsed, s.get_debug().pops_fired);
    }
    return 0;
}
catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
