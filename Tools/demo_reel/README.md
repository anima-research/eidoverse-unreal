# Underwater demo reel

An extensible, nine-cut feature tour of the current L_Epipelagic demo.
65 seconds, 1920×1080, 30 fps. Silent, with optional burned-in explanatory titles.

## Deliverables

Default output folder: `/Users/olena/unrealtest/demo_reel/output/`

- `Underwater_Demo_Reel_01.mp4`: captioned H.264, broadly playable, fast-start.
- `Underwater_Demo_Reel_01_clean.mp4`: clean picture without titles.
- `clips/`: independently replaceable clean clips.
- `captions.ass`, `captions.srt`: editable caption tracks.
- `timeline.json`, `clips.ffconcat`: edit/timing references.

The original PNG frames remain in `/Users/olena/unrealtest/demo_reel/final/`.
They are not copied into the Git repository. Keep or archive these if lossless
re-encoding is important; the MP4s are sufficient for ordinary viewing/editing.

## Editable source

`shots.json` defines shot order, duration, camera/look-at keyframes, lens length,
optional camera torch, and explanatory text. The coordinate system is Unreal cm.
The bottom-life shot anchors to a placed crab instance, so check that framing again
after regenerating ecology.

Unreal assets are under `/Game/Underwater/Cinematics/DemoReel/`:

- `LS_DemoReel_Master`: assembled shot track for reviewing/editing in Sequencer.
- `LS_01_habitat` through `LS_09_platform`: editable individual camera sequences.

The movie is a **cinematic feature tour, not a keyboard-input replay**. Fish,
vegetation, particles, optics, and ocean waves run in the real game world.
Camera moves, the submarine ascent and the camera's ladder route are authored
in Sequencer. Render-only camera torches illuminate detail shots. Calibration
rigs are hidden by sequence visibility tracks; they remain in the gameplay map.
The capture does not change terrain, materials, gameplay logic or map placement.

## Render or extend

Movie Render Queue is enabled in `Test1.uproject`. It is an Epic engine plugin.
FFmpeg and FFprobe are required for assembly; the installed FFmpeg includes libass.

1. Edit `shots.json`. Add a new unique ID to append a shot, or change titles without
   re-rendering pictures. Camera keys use `[seconds, [x,y,z], [look_x,look_y,look_z]]`.
2. Stop PIE and run these in Unreal's **Cmd** field:

   ```text
   py "/Users/olena/Documents/Unreal Projects/Test1/Tools/demo_reel/build_reel.py"
   py "/Users/olena/Documents/Unreal Projects/Test1/Tools/demo_reel/build_master.py"
   ```

   Wait for each `REEL_BUILD_SUCCESS` / `REEL_MASTER_READY` log before the next step.
   These regenerate the reel's own sequence assets. Do not put unique manual edits
   into those generated assets without first duplicating them or updating the recipe.

3. Run `render_preview.py` for one low-resolution frame per shot. Review framing,
   then run `render_final.py` for full-resolution PNG sequences. Render completion is
   recorded in `render_status.json` in the chosen output directory.
4. Assemble from a terminal:

   ```sh
   python3 "/Users/olena/Documents/Unreal Projects/Test1/Tools/demo_reel/assemble_reel.py" \
     --frames /Users/olena/unrealtest/demo_reel/final \
     --output /Users/olena/unrealtest/demo_reel/output
   ```

   Use `--force` intentionally to replace existing encoded clips/masters after a
   revision. Assembly validates expected frame counts and consecutive numbering.

For selective rendering, set `REEL_SHOTS` to comma-separated shot IDs before running
`render_reel.py`. `REEL_MODE=final` chooses final resolution; `REEL_OUTPUT` selects
a fresh frame-output directory. `REEL_CHECK_FRAME` selects a preview frame.
Avoid rendering into a nonempty shot directory: MRQ will otherwise suffix filenames
instead of overwriting them. Use a fresh version directory, then copy/assemble the
desired shot versions into the final edit.

## Capture settings / limitations

- A separate MRQ queue leaves the user's editor queue intact.
- One spatial/temporal sample per frame, existing project AA, 100% screen percentage.
- 60 engine warm-up frames, rendered for GPU particles, plus 24 render warm-up frames.
- Motion blur disabled for readable graphics inspection; existing scene exposure and
  rendering quality are retained. CVars are restored when each job finishes.
- Independent jobs restart world simulation. This makes clip replacement convenient,
  but the movie does not claim continuous wildlife/ocean simulation across cuts.
- The static set can be previewed in Sequencer; runtime schooling needs PIE/MRQ.
- This is an offline render, not a benchmark or a claim of real-time frame rate.

After assembly, run `validate_reel.py` with the same `--frames` and `--output`
arguments. It checks frame counts, continuity, picture changes, codec, resolution,
duration and full-file decoding, and writes a contact sheet plus `validation.json`.
The validator uses standard Python and FFmpeg only.

## Water revision / reel 02

`render_water_revision.py` rerenders only surfacing, boarding, and platform cuts
into `demo_reel/water_revision_02/`. `assemble_water_revision.py` reuses the first
six clips and creates `demo_reel/output_02/Underwater_Demo_Reel_02.mp4` plus its clean
master. Original reel 01 and raw frames are preserved. `revision_sources.json`
records precisely which frame version each cut uses.

Generic assembly and validation accept `--edition 2` (default: 1). The revision's
combined frame folder contains links to the original or revised shot directories.
Do not use `--force` on linked reusable clips unless intentionally regenerating
them; use a fresh output directory for another revision.

Water implementation and test notes: `../vessel_redesign/WATER_SURFACE_REVISION.md`.

Epic references used to set up the pipeline:

- https://dev.epicgames.com/documentation/unreal-engine/movie-render-pipeline-in-unreal-engine
- https://dev.epicgames.com/documentation/unreal-engine/using-command-line-rendering-with-move-render-queue-in-unreal-engine
