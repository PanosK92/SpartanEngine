# Procedural engine audio

Run `.\tools\audio_tests\run.cmd` from the repository root. It builds the production DSP with MSVC C++20, optimizations, and warnings as errors, then writes 48 kHz stereo PCM previews under `binaries/audio_tests/after`. An optional argument selects another output directory. It uses the repository's Assimp library for XML parsing, without starting the editor.

Each 12-second preview covers idle, a loaded RPM sweep with a shift, the limiter, throttle release, a cabin transition, and shutdown. These are repeatable listening fixtures driven by each `.car` file; they are not recordings of an actual drive. Compare at matched listening volume. The final character still needs listening in the game with its distance, tire, and environmental mix.

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
