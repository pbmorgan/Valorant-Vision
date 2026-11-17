# Build the reorganized project on Windows

Extract the clean ZIP into a **new folder** first; keep your original working project unchanged until the rebuilt application passes its tests.

## Required model that could not fit the source-only ZIP

Copy this file from your **original** project:

`Valorant-Vision-clean\model\faster_rcnn.onnx` (approximately 159 MB)

Place it in the extracted cleaned project's `model` folder, under the same filename. **The build will stop with a descriptive CMake error if this file is absent.** All other model files from the supplied source ZIP were preserved.

## Build with MSVC

Open PowerShell, change to the **extracted cleaned project folder**, then run:

```powershell
cmd.exe /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B out\build\release && "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out\build\release --config Release --target Valorant-Vision assist_detection_test playertracker_test assist_pipeline_test'
```

This uses your existing Visual Studio 2022 paths. It may require the same vcpkg dependencies and environment settings as before.

## Run tests and the application

```powershell
cd .\out\build\release
.\playertracker_test.exe
.\assist_pipeline_test.exe
.\Valorant-Vision.exe
```

If Qt reports that the `windows` platform plugin or multimedia backends are missing, copy the original Qt plugins from your vcpkg `installed\x64-windows\Qt6\plugins` tree into the executable's `platforms` and `multimedia` subfolders, as in the previous setup.

**Important:** A source-level refactor does not prove the app runs correctly. Validate real match playback, the 30-second manual team-name prompt, roster population, kill/death counting, and the still-experimental assist detection.
