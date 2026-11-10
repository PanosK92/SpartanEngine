# Testing and CI

## Start-up smoke test

```bash
python tools/ci_smoke_test.py binaries/spartan_vulkan.exe
```

[`tools/ci_smoke_test.py`](../tools/ci_smoke_test.py) runs the executable with `--ci-smoke-test`. In that mode the engine starts without a window or GPU, initializes the CPU-side systems (resources, thread pool, physics, world), runs ten idle ticks and exits, logging `STARTUP_SMOKE_TEST_PASSED`. The script passes when the process exits cleanly, the marker is in the log and no `Error:` lines were written. It saves the log to `binaries/logs/smoke_test_*.txt`. This is what CI runs on every platform, since CI machines have no GPU.

## GPU smoke tests

```powershell
.\spartan_vulkan_development.exe --headless -ci_test --ci-exit
```

`-ci_test` runs the suite in [`source/testing/SmokeTest.cpp`](../source/testing/SmokeTest.cpp) once the first frame has rendered: RHI resource creation, shader compilation, rendering and reading back a test scene, and play/stop state restoration. `--ci-exit` closes the engine when it finishes; results go to the log. The MCP tool `headless_smoke_test` runs the same suite in a separate hidden instance.

## GitHub Actions

[`.github/workflows/workflow.yml`](../.github/workflows/workflow.yml) runs on every push except documentation-only changes:

| Job | Runner | Steps |
| --- | ------ | ----- |
| `build` | Windows Server 2025, matrix `d3d12` and `vulkan` | Premake `vs2026`, MSBuild `release`, smoke test, zip |
| `build_webgpu` | Windows Server 2025 | Premake `vs2026 webgpu` (fetches Dawn), MSBuild `release`, smoke test, zip; bakes the WGSL cache on WARP, then builds the browser package with emsdk and zips it ([details](webgpu.md#ci)) |
| `build_linux` | Ubuntu 24.04 | `linux_dependencies.sh`, `gmake` release, smoke test, archive |
| `build_macos` | macOS 26 | Xcode 26, `macos_dependencies.sh`, Metal build, bundle libraries, smoke test, archive |
| `release` | Pushes to `master` when every build passes | Publishes a GitHub release with all six builds (Windows Vulkan, D3D12 and WebGPU, the web package, Linux and macOS) and a changelog |

## Before submitting a change

1. Build in `development` and `release` (Windows treats warnings as errors).
2. Run the start-up smoke test.
3. Load a few bundled worlds, play and stop, and check the console for errors.
4. For renderer changes, compare screenshots before and after (the MCP `screenshot_compare` tool helps) and check both DirectX 12 and Vulkan where possible.
