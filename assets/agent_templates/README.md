# VALORANT agent templates for kill-feed tracking

**29 agent portraits · main-kill-feed sizes · assist-icon sizes · OpenCV masks**

These assets were cropped and resized from the PNG portraits you uploaded. They are **not AI-redrawn**; no agent details were added or invented. The source portraits are retained under `originals/`.

## Files

- `templates/main/{WxH}/{agent}.png` — cropped RGBA main killer/victim portraits, sizes `40x28`, `48x32`, `56x40`, `64x44`.
- `templates/assist/{WxH}/{agent}.png` — cropped RGBA assist portraits, sizes `16x16`, `20x20`, `24x24`, `28x28`.
- `masks/{main|assist}/{WxH}/{agent}.png` — matching 8-bit alpha masks, for `cv::matchTemplate`.
- `team_backgrounds/{red|teal}/{main|assist}/{WxH}/{agent}.png` — reference images composited against approximate in-game team colors. These are optional and colors may differ in your own videos.
- `originals/{agent}.png` — the 29 untouched 1000×1000 source images.
- `manifest.json` — agent names, crop centers, exact source cropping coordinates and template sizes.
- `preview/contact_sheet.png` — human-readable overview.
- `examples/find_agent.cpp` — a small standalone C++ / OpenCV matching example.

Filenames are lowercase: `kayo.png` for KAY/O; other examples: `neon.png`, `chamber.png` and `omen.png`.

## Recommended first experiment

1. Start with a **full-resolution frame from the same video** you plan to analyze, not just the low-resolution example screenshots. Crop the killer/victim portrait region first; avoid OCR text, weapons and other UI. For the tiny assist icons, crop their separate area. Skip a region if there is no icon.
2. Try `templates/main/48x32/` and `templates/assist/20x20/` first. If they do not match the captured pixel sizes, test the other dimensions. Don't run the entire video frame against all sizes unnecessarily.
3. Use the grayscale source `cv::IMREAD_UNCHANGED` (4-channel BGRA) with matching alpha mask. A simple example appears in `examples/find_agent.cpp`. Consider multi-frame agreement, a confidence margin versus the second-best match, and stricter thresholds from labeled video samples.
4. If the feed artwork is scaled, tinted, cropped or otherwise rendered differently, **recalibrate the crop centers and dimensions against clean examples**. The two provided screenshots are too blurry for pixel-accurate calibration. A face-heavy crop works better here than shrinking each entire 1000×1000 icon.
5. Store a player-to-agent map separately. A kill feed identifies the killer/victim by name, but the assist portrait itself usually has no username. Infer the username from other reliable observations (e.g. scoreboard) and join the map to the detected assisting agent.
6. Count each persistent kill-feed entry only once; use a deduplication rule across frames.

**Important limitation:** These are provisional portraits, not templates extracted pixel-for-pixel from the game. Template matching on low-resolution video may need new crops, blur/scale adjustments, and validation. An icon detection alone does not prove an assist belongs to a particular username until your player–agent map is grounded.

## Example: match a specific region

Build a standalone example (adjust path to your OpenCV installation):

```bash
g++ -std=c++17 examples/find_agent.cpp -o find_agent $(pkg-config --cflags --libs opencv4)
```

Usage (path to screenshot and region within screenshot):

```bash
./find_agent full_frame.png templates/main/48x32 0 0 400 100
```

Replace `0 0 400 100` with your cropped region `(x, y, width, height)`.

The C++ example lists the best agent score and also prints the top 5 candidates. Scores are **not calibrated probabilities**; threshold and score margin require testing on representative gameplay.

## Suggested repo location

Place this directory at `assets/agent_templates/` (you can rename it). If you only want to commit the deployable files, commit `templates/`, `masks/`, and `manifest.json` first. The full-size `originals/` and examples are optional.

## Adjusting icon framing

If one agent's face or helmet is slightly off-center, edit its `center_px` in `manifest.json`, then run:

```bash
python -m pip install Pillow
python tools/regenerate_templates.py
```

This rebuilds the cropped templates, masks and optional team-color examples from the unchanged `originals/`. It does not rebuild the static contact sheet.
