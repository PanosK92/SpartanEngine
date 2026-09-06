# Editor UI validation

The editor redesign uses the existing ImGui widget system. The default palette is graphite with icy light blue interaction states (#70D4FF). The workspace has no footer; renderer and world data formats are unchanged.

Build from the repository root using Visual Studio MSBuild:

```powershell
MSBuild.exe spartan.vcxproj /p:Configuration=development /p:Platform=x64 /m
```

Launch `binaries/spartan_vulkan_development.exe`, the same target used by Visual Studio. An optional `/p:TargetName=spartan_ui_validation` can isolate a test build while another executable is in use, but it does not update the normal editor: rebuild the default target before handing off a fix. The build uses the normal editor settings beside the executable; back up `editor.ini`, `imgui_style_user.bin`, `spartan.xml`, and `spartan_worlds.xml` before testing personalized layouts.

Manual regression checks:

- On first use, the launcher starts within the main work area. Drag it outside the editor and onto another monitor: it must become a separate platform window. Resize the main editor, close/reopen the launcher, and restart the editor: its chosen position and size must be preserved, and its size must not be capped by the main viewport. At short heights its content scrolls and Open World remains reachable.
- At 1280 x 720 with 150% display scaling, confirm the main toolbar changes to Tools. Check transform operations, coordinate space, snapping, panels, screenshot and RenderDoc entries.
- Use View > Reset workspace layout. Check that hierarchy and inspector remain readable, and that Assets navigation and view actions occupy separate rows when space is limited.
- Play, Pause and Stop. Check the button label, viewport badge and read-only inspector notice. Pause is disabled outside playback. The docked workspace should extend to the bottom of the window without a status footer.
- Load the Ferrari showcase. Check that the empty viewport message disappears, entity counts populate, and the scene renders normally.
- Open View > Developer > Style. The previous factory palette should show the new Spartan preset. Change and reset presets at 150% scaling; controls must retain their size, and the window must permit scrolling.

On 2026-09-06 the development/x64 Vulkan build and live launcher, workspace reset, compact toolbar, playback state and Ferrari scene checks passed on a 3840 x 2160 display at 150% scaling, including a restored 1280 x 720 window. The build reports existing third-party FreeImage import and missing NRD/NRI debug-symbol warnings. Other graphics backends were not exercised.

The default sRGB palette has 16.7:1 primary text contrast on the canvas and 5.9:1 muted text contrast on the control surface. Bright primary buttons use dark labels, with lighter hover and deeper pressed states. HDR appearance also depends on the engine's SDR-white setting and display capture conversion.

The previous factory palette is upgraded in memory without saving over the existing theme file. Other base palettes stay loaded. Fine-grained overrides made only through ImGui's developer color editor should be backed up before explicitly saving the new Spartan preset.
