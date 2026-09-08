# Resolution selector regression

The old selector returned preset index zero whenever the actual render or output dimensions were absent from the monitor mode list. XeSS/DLSS's existing aspect correction can turn 1920x1080 into 1920x938 for a 3180x1555 editor output, so the UI falsely showed the first preset (4K).

The combo now previews the renderer's actual dimensions, including custom sizes, and changes them only on selection. The additional Active render resolution row includes resolution scaling. Presets are deduplicated by dimensions across refresh rates: selecting a texture size does not switch the monitor's refresh rate.

Run `cmd /c tools\render_options_tests\run.cmd` from the repository root with Node.js and Visual Studio C++ tools installed. The test extracts the production combo callback, preset population and vendor resolution sanitation function. An ImGui interaction recorder verifies:

- Selecting 1080p for a 4K output remains 1920x1080 in all five AA/upscaling modes.
- XeSS/DLSS aspect fitting displays 1920x938 for the custom viewport example, with no repeated writes or false 4K label over 100 frames.
- Empty preset lists and closed combos still show actual dimensions.
- Resolutions advertised only at another refresh rate remain selectable, without duplicate dimensions.

These checks and the development/x64 editor build pass. The recorder tests UI data and interactions; it does not initialize the XeSS/DLSS SDKs. The existing vendor aspect/scale policy is preserved. Intel recommends keeping the input aspect close to the output aspect in its [XeSS SR developer guide](https://github.com/intel/xess/blob/main/doc/xess_sr_developer_guide_english.md).
