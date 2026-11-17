# Valorant-Vision — Codebase organization pass

**Basis:** User-provided November 2026 source ZIP, not the older GitHub backup.

## Organization

- `cpp/ocr.cpp` — OCR lifecycle and frame handling
- `cpp/ocr_frame.cpp` — one-frame processing orchestration
- `cpp/ocr_replay_spike.cpp` — replay state, Spike detection and countdown
- `cpp/ocr_teams.cpp` — team name OCR and proposal
- `cpp/ocr_scores.cpp` — score OCR and round score transitions
- `cpp/ocr_round_timer.cpp` — round timer OCR and synchronization
- `cpp/ocr_text.cpp` — general and killfeed OCR preprocessing
- `cpp/ocr_killfeed_matching.cpp` — token resolution and player-name matching
- `cpp/ocr_killfeed.cpp` — killfeed row change detection and orchestration
- `cpp/ocr_killer_agents.cpp` — killer name box and portrait matching
- `cpp/ocr_assists.cpp` — assist icon candidate detection
- `cpp/ocr_victim_agents.cpp` — victim portrait matching
- `cpp/ocr_kill_events.cpp` — kill logging and optional image snapshots
- `headers/ocr_layout_internal.h` — shared broadcast coordinates
- `cpp/mainwindow_teams.cpp` and `cpp/mainwindow_video.cpp` — extracted from main window
- `cpp/playertracker_stats.cpp` and `cpp/playertracker_assists.cpp` — extracted from tracker
- `cpp/webScraper_refresh.cpp` — isolated region refresh and UI prompts
- `cpp/minimap_detection.cpp` — extracted map recognition from renderer

## Cleanup

Excluded 93 generated/root snapshot entries or redundant patch/backup material. See `cleanup_manifest.txt`. None of the referenced model ONNX files, agent templates/masks, map layouts, seed player data, Tesseract language files or active unit tests were intentionally removed. The standalone assist test and playertracker/pipeline tests remain separate CMake targets.

**Debug output behavior intentionally unchanged:** Some code still writes debug images. Removing those writes or changing debug thresholds would be a functional change. Runtime images should now be ignored by Git using the expanded `.gitignore`.

## Important Windows build verification

This container cannot run the project against the Windows MSVC, Qt and vcpkg packages, so the result is a **source-level refactor candidate** and is **not claimed to be build-verified**. Compile using the original Visual Studio x64 environment, preferably with a fresh CMake configure because the source list changed. Then run:

1. `playertracker_test.exe`
2. `assist_pipeline_test.exe`
3. `Valorant-Vision.exe` with a match video, verify team prompt/timeout, roster, kills/deaths, assists and minimap.

See `WINDOWS_BUILD.md` for commands.

## Missing large asset in the upload

`model/faster_rcnn.onnx` (158.61 MB according to the supplied inventory) was filtered out of the uploaded source ZIP. It is **not** unused. Its original copy must be supplied by the user before the reorganized tree can build or run as a full application. The CMake configuration now reports a descriptive error when it is missing. No replacement model is bundled.

An optional Tesseract PDF-export font was not packaged; the OCR language models are preserved.
