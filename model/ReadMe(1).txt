Next steps:

In its current state, the model generates a synthetic map containing agents, drawn abilities, and ability icons.
A smaller selection of icons was chosen for testing purposes in order to prioritize core functionality. The remaining icons are included in the file labeled Val_icons. Additionally, all files within Val_icons will need to be converted to PNG format. Moving forward, I would recommend creating a Python file similar to add_agent for utilities. The add_agent implementation was created to simplify the process of adding new agents. Since each agent possesses numerous abilities, managing utility implementation individually would become increasingly tedious over time.

Secondly, the COCO files for Agents and Utilities are kept separate. This prevents information from one dataset from cluttering the other and should eventually allow them to run concurrently. However, at the moment, the neural network does not recognize the utilities_rcnn.onnx model file. This occurs because it is not copied into the Release directory and instead remains in the Model folder, where it is not visible to the application.

I would suggest starting here when utilities are not appearing on the map. Due to utilities_rcnn.onnx not being copied into the Release directory, the program is unable to access the utility model and is effectively starved of utility data. Because of this, utility information is never displayed, even though the rest of the pipeline continues to function correctly. The program was therefore reverted to a state where everything still compiles successfully and the model continues to generate all required data. When executed, the program will currently display only agent movement. While utility_data is still collected, it is effectively unusable in its current state, as the utility model is never loaded and the data is therefore never surfaced within the application.

A minor suggested improvement regarding drawn lines is noted within image_generator. This is primarily a performance optimization rather than a necessary change. Furthermore, this issue only occurs on one map, so it was deemed low priority.




