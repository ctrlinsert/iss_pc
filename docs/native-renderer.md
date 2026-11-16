# Native renderer

Goal: replace the game's render passes, one at a time, with our own Vulkan passes and shaders, so
that in the end the PS4 command stream and the shadPS4 shader recompiler are no longer needed for
drawing. Until a pass is native, the current renderer v2 path draws it, and any pass can be turned
off on its own.

Off by default. Nothing changes unless `ISS_NATIVE_RENDER` names a pass.

## How a native pass runs

1. A game hook at the start of one of the engine's render passes writes a **BEGIN** marker into the
   draw command buffer, and one at its end writes the **END** marker. Both are PM4 NOP packets
   (`'ISN'` magic, `gpu_v2/gpu/native_render_core.h`), so a renderer that does not know them, or a
   pass that is switched off, sees nothing different. The BEGIN payload carries what the hook read
   from the engine's own objects: the guest addresses and sizes of the pass's images.
2. The command processor, at BEGIN, checks the commands up to the matching END (`ScanSpan`). Only
   when every packet between is register state, index or instance state, a draw or a dispatch,
   a cache flush or a cache sync (ACQUIRE_MEM), or one of gnm's hints the command processor ignores (an L2 prefetch, a target size hint, a register-update hint, the native-passes token after a draw) may the pass run natively. A packet that writes or waits on memory, chains to
   another buffer, counts samples (occlusion) or is another marker keeps the engine's commands.
3. The native pass resolves its images through the texture cache and records its own work, with
   sizes taken from those images. If it cannot (an image not in the cache, a format it does not
   handle), it declines and the engine's commands run: that is the fallback.
4. When it ran, the command processor still interprets every packet of the span, so registers,
   render target hints and everything after END see exactly the state the engine left; it only
   drops the span's draws and dispatches.

A pass can also replace its span's draws one at a time (a draw handler). The command processor
then calls the pass at each draw of the span with the engine's registers for that draw applied
(its shader, user data, targets), so the pass reads its inputs exactly where the engine's shader
would. If the pass declines a draw, only that draw falls back to the engine's.

A pass never runs in the pause menu (pause guard), with the split command processor, or while the
temporal upscaler redirects the UI to output-size images.

## Settings

| Variable | Meaning |
| --- | --- |
| `ISS_NATIVE_RENDER` | Passes to run natively: names separated by commas, `all`, `-name` to remove one (`all,-bloom`). Empty, `0`, `off`: none (default). |
| `ISS_NATIVE_RENDER=probe` | Diagnostic: every bracketed span is checked and reported, nothing runs natively and nothing is dropped. For checking a new hook before its pass exists. The probe scans the whole span, past unsafe packets: the log shows the first unsafe packet's words, how many there are, the draws, the engine pixel shader (`ps_hash`, the name its `BB_DUMP_SHADERS` dump has: `fs_0x<hash>`), and the shader of every draw and dispatch in order (`draw_ps`, `dispatch_cs`), since a multi-stage pass such as SMAA uses a different shader per draw. |
| `ISS_NATIVE_RENDER_LOG=1` | Prints the first spans of each pass (`NATIVE_RENDER span pass=... draws=... scan=...`) and why a pass declined. |

At most every 10 seconds (on a BEGIN) the log has one `NATIVE_RENDER report` line per pass: how
often it ran natively, declined, was blocked, and how many draws and dispatches it replaced. The
game side adds `NATIVE_RENDER hooks` at the first bracketed call and every 1024 after: calls per
call site and markers written, so a marker that is written but never reaches the renderer shows.

## Rules for every pass

- **Any output size and aspect.** Sizes come from the bound images at run time; no resolution,
  aspect or tile count is written into a shader or a constant. Downsample chains use `Level()`,
  texel sizes `InverseSize()`, round shapes `Aspect()` (`native_render_core.h`). Tests cover
  1280x800 through 7680x2160.
- **Written from a description, not from game code.** A pass's shader is written fresh from notes
  on what the pass does (its inputs, outputs and math in plain words). Decompiled code, dumped or
  disassembled game shaders, and anything generated from them stay out of the repository.
- **Measured against the fallback.** A pass ships once its output matches the engine's on the same
  frames within an agreed tolerance, at no frame-rate cost.

## Status

| Stage | State |
| --- | --- |
| Markers, span check, switch, fallback, command processor suppression | done (`native_render_core.h`, `native_render.cpp`, `liverpool.cpp`) |
| Game hooks: the post chain's FXAA and texture-copy draws bracketed (`src/iss_native_render_hook.h`, installed only when `ISS_NATIVE_RENDER` is set) | done; check in game with `ISS_NATIVE_RENDER=probe ISS_NATIVE_RENDER_LOG=1` |
| Hooks around PostBlend and SMAA T2x, the per-frame passes (the first probe runs showed FXAA and the copy are not per frame: the default anti-aliasing is SMAA T2x) | done; probe to confirm |
| First native pass: PostBlend (`postblend`, `native_postblend.cpp`, `iss_native_postblend.frag`) | written; needs an in-game comparison against the engine's |
| Native copy pass (`copy`): frame capture only | later |
| Native FXAA (`fxaa`), written from the public FXAA 3.11 algorithm | after the copy |

## PostBlend

The colour composite before anti-aliasing, every frame. At its one draw the pass finds the engine
shader's six images (distortion, scene, bloom, two 3D grading tables, dither noise) and its constant
buffer through the resource list the shader was recompiled with, and draws a full-screen triangle
with its own shader into colour target 0:

1. distort the coordinates by the distortion map;
2. scene times exposure, plus bloom;
3. desaturate toward grey, and floor each channel at a share of luma;
4. a power curve on luma (gain and exponent below and above a knee), applied as a ratio so hue stays;
5. vignette;
6. the public Narkowicz fit of the ACES filmic curve, then the sRGB transfer function;
7. the first grading table, blended toward the second by the game's amount;
8. luma into alpha (the anti-aliasing reads it), and a 1/255 dither.

The frame's values (exposure, curve, grading) come from the game. Sizes and shapes do not: the
vignette keeps the game's vertical scale and takes its horizontal one from the target's aspect, so it
is round in pixels at any aspect, and the dither noise tiles one texel per target pixel at any
resolution (`native_postblend_core.h`). It declines (the engine's draw runs) for another shader, a
different resource list, a write mask or blending, a target that is not 8- or 10-bit UNORM, or a
scene that is not the target's size. Try it with `ISS_NATIVE_RENDER=postblend ISS_NATIVE_RENDER_LOG=1`:
the log prints the first native draw's target, the game's values, the vignette scale and the noise
tiling, and each fallback's reason.

Unit tests: `port/tests/test_native_render.cpp` and `test_native_render_hook.cpp` (ctest `native_render`,
`native_render_hook`; the hook test also checks the sites against the real eboot when `ISS_TEST_EBOOT_ELF`
names the unwrapped 1.07 ELF).
