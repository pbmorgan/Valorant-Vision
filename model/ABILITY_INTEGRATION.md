# Ability Beta integration (experimental)

This branch adds an OPTIONAL second Faster R-CNN detector for Valorant utilities.
It does not replace the agent detector, OCR, scoring, or minimap map-matching.

## Missing required model

**The Ability-Beta branch DOES NOT include trained utility ONNX weights.**
Ask the model-team developer for their exported `utility_rcnn.onnx`, or train
and export it with `model/util_rcnn_training.ipynb`. The notebook's export
filename (singular `utility_rcnn.onnx`) is the one used by this integration.
Copy it to `model/utility_rcnn.onnx`. Re-run CMake configuration and build.
CMake will copy the utility model into the executable folder when available.

If absent, the program logs an agent-only fallback and runs normally.
Agent `faster_rcnn.onnx` MUST NOT be replaced with Ability-Beta's version.

## Runtime flow

QVideoSink -> VideoPlayer -> ModelWrapper (same 384x384 minimap crop) ->
agent NeuralNetwork + optional utility NeuralNetwork -> Qt signals -> Minimap.
Utility overlays are orange and drawn underneath agent icons; supported PNG
icons are used when available, otherwise a small label or bounding box appears.

## Training

Run `python generate.py` from `model/` with prerequisites in requirements.txt.
The repaired wrapper now writes both `instances_agents_{train,val}.json` and
`instances_utilities_{train,val}.json` for the same synthetic images.
The notebook's class-count loading has been changed to the utility registry.
Training and model accuracy have NOT been validated here.

## Validation

1. Configure and build in a VS Developer environment.
2. Start with no utility ONNX and confirm agent tracking still works.
3. Put the real exported utility ONNX in model/, reconfigure and rebuild.
4. Run from the exe directory, confirm `[Utilities] ONNX model loaded`.
5. Play known utility frames and compare predictions with ground truth.
6. Check behavior on replay footage and video changes.
7. Expect slower model updates: two Faster R-CNNs run sequentially on one thread.

This integration was prepared without access to the developer's utility model
or a Windows compiler, so it requires local compilation and end-to-end tests.
