# Sponza world regression checks

`builder.lua` executes the production world builder with lightweight engine mocks.
It checks that model lights are not imported, lamp intensity uses explicit lumens,
orientation helpers do not become duplicate lamps, and a saved hierarchy is reused
without resetting its edited transform or lamp intensity.

From the repository root, use a Lua 5.4 interpreter:

```powershell
binaries/backrooms_tests/lua.exe tools/sponza_tests/builder.lua
```

The existing `tools/backrooms_tests/build.cmd` can produce that interpreter from
the bundled Lua library if it is missing.

`roundtrip.mjs` is an integration check for an **isolated** engine on port 47780
loaded with `binaries/sponza_tests/sponza_fixed.world`. That fixture is a copy of
`worlds/sponza.world` with its builder script path made absolute. Do not run this
against an editing session: it changes the diagnostic sun, saves a separate
`sponza_roundtrip.world`, and reloads that copy.

```powershell
node tools/sponza_tests/roundtrip.mjs
```

It verifies one directional light, 22 lamps at 1600 lumens, constant entity/light
counts after saving and loading, and preservation of edited sun rotation and
shadow settings. It also captures shadow on/off images for visual inspection.
Outputs go under `binaries/sponza_tests` and the engine screenshot directory.
