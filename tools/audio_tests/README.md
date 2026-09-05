# Procedural engine audio

Run `.\tools\audio_tests\run.cmd` from the repository root. It builds the production DSP with MSVC C++20, optimizations, and warnings as errors, then writes 48 kHz stereo PCM previews under `binaries/audio_tests/after`. An optional argument selects another output directory. It uses the repository's Assimp library for XML parsing, without starting the editor.

Each 12-second preview covers starter cranking, ignition catch and idle settling, a loaded RPM sweep with a shift, the limiter, throttle release, a cabin transition, and shutdown. `tire_scrub_slide_release.wav` covers silence, light scrub, a full slide, and release. These are repeatable listening fixtures driven by the production synthesizers; they are not recordings of an actual drive. Compare at matched listening volume. The final character still needs listening in the game with its distance, tire, and environmental mix.

Startup is an acoustic sequence triggered once when the occupied car's stream starts. It uses the current spec's compression, inertia, displacement, firing order and exhaust, then hands over to live RPM in roughly 1.5–1.9 seconds. It does not delay or change drivetrain physics. The old `binaries/project/music/cars/car_start.wav` asset and its source are removed. Door open/close remains a separate recorded effect.

Tire friction uses four independently moving contact regions with phase-linked fundamental and harmonics, pressure flutter, and broadband scrub. This replaces the rejected narrow-band-noise version, which sounded like a whistle. Grounded-wheel slip, load and tread speed control onset, including stationary burnouts and locked-wheel braking; a single sample-smoothed envelope sets intensity. Release timing is independent of game frame rate. Only the occupied car drives the shared tire synth.

The second sound-design pass was measured against the public previews of [audible-edge's recorded Chrysler LHS cornering squeal](https://freesound.org/people/audible-edge/sounds/71738/) (CC0) and [davidbain's engine start](https://freesound.org/people/davidbain/sounds/209864/) (CC BY 4.0). References live only in the ignored `binaries/audio_tests/references` analysis folder; neither is included in game playback. Measurements use 2048-sample Hann windows and 512-sample hops. The tire reference at 6.05–7.3 seconds puts approximately 78% of its energy in 800–1600 Hz and 16% in 1600–3200 Hz; the revised sustained synth puts 82% and 17% there. Its dominant pitch moves across roughly 914–1195 Hz (10th–90th percentiles), versus 1125–1172 Hz for the rejected version. The real reference varies more widely, so these are timbre constraints, not an exact reconstruction.

The starter reference at 0.04–0.45 seconds splits roughly 30/28/37% of its energy across 80–400/400–800/800–1600 Hz. The rejected starter had 99.6% below 400 Hz. The revised Porsche startup gives approximately 33/26/39%, using compression-modulated gear harmonics and filtered brush noise; the starter pinion no longer follows the ignition flare up to idle RPM. These numerical checks cannot substitute for listening. Use `python tools/audio_tests/analyze_reference.py file.wav --start 2.1 --end 2.9` (NumPy required) to repeat the measurements on PCM16 WAVs.

Additional regressions cover startup handover and re-entry, spec sensitivity, tire mono/stereo and callback-size consistency, silence after release/reset, slip-dependent energy, non-finite controls and sample rates from 8–192 kHz.

The regression suite checks callback-size independence, mono/stereo consistency, repeatable reset, engine-off silence, supported sample rates, geometry sensitivity, even and explicitly uneven cranks, live reconfiguration, disabled afterfire, non-finite telemetry, and high-RPM/high-gain headroom for one through sixteen cylinders. It also exercises a turbo throttle release with smoothed controls. A separate executable runs the actual production preset loader on all cars, verifies the new authored fields, and rejects a crank whose intervals do not sum to 720 degrees. Rendering speed is printed relative to real time; this excludes graphics, simulation, SDL, and reverb costs.

## Sound model

Cylinder pressure pulses retain fractional crank timing and excite separate primary pipes, bank collectors, mufflers, and tailpipes. Compression, bore/stroke, displacement, load, boost, and upgrades shape combustion. Small cycle pressure variations and inertia-dependent crank ripple give movement without jittering the authored firing angles. Exhaust pulse tails reach zero smoothly. Broadband combustion texture is low-pass filtered and quiet.

The intake is driven by crank-synchronized valve-flow pulses and a resonant runner. Only a small flow-dependent turbulence component is added. A turbo has shaft tones and a bypass release; compressor surge is reserved for a configuration without a bypass valve. Mechanical noise sits below the combustion layers. Physical airflow and transient exhaust pops are intentionally audible; “clean” does not mean suppressing every non-tonal component.

The DSP runs at twice the output rate. A 95-tap windowed-sinc FIR filters the final signal before decimation, including nonlinear saturation and limiting. This reduces aliasing from narrow combustion pulses and distortion. The mid/side mix preserves bank identity and mono compatibility. Camera filters use the actual control update rate, while playback volume/pan use a 5 ms sample-wise slew. Live spec changes fade down for 12 ms before installing empty pipe buffers and fade back up; this is a short dip rather than a full dual-engine crossfade.

SDL playback is primed before binding the stream. The existing roughly 43 ms queue remains main-thread-fed: long rendering stalls can still underrun it. The standalone DSP tests cannot prove absence of device clicks under graphics load. A future audio-thread producer would need synchronized runtime parameters, meters, WAV capture, and reset ownership; moving the current callback to another thread alone is not safe.

## Additional car specifications

Optional attributes on `<engine>` are backward compatible; current cars do not acquire invented manufacturer measurements.

- `intake_runner_length_m`: effective acoustic length in meters, 0–2. Zero estimates `0.08 + 3 * stroke_m` (clamped internally to 0.08–2 m). Supply a measured effective runner length when available.
- `intake_valve_duration_deg`: intake opening duration in crank degrees, 120–320, default 220. This acoustic approximation does not yet model variable valve timing.
- `engine_combustion_variation`: fractional pressure variation, 0–0.2, default 0.025. Zero removes cylinder/cycle pressure variation.
- `engine_firing_intervals_deg`: intervals after each event in `engine_firing_order`, each at least 1 degree, totaling 720. Omitted/all-zero means even firing. Example for an odd-fire six: `90,150,90,150,90,150`. Bank angle alone is insufficient to determine crankpin timing.
- `turbo_bypass_valve`: default true. False allows compressor surge on lift instead of the normal bypass release. It is an acoustic configuration, not a turbo durability simulation.

The existing `engine_inertia` now also damps acoustic crank ripple. Existing firing order, cylinder-to-bank mapping, exhaust geometry, compression, dimensions, RPM, boost, and upgrade specs remain active. Unknown intake geometry uses an explicit approximation; more exact recordings and measured geometry would be needed to calibrate a particular real car.

Background: [Julius O. Smith, nonlinear elements and aliasing](https://www.dsprelated.com/freebooks/pasp/Nonlinear_Elements.html), [SDL queued-audio semantics](https://wiki.libsdl.org/SDL3/SDL_GetAudioStreamQueued).
