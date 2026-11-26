Next steps:

In it's current state, the model generates a synthetic map agents, drawn abilities, and ability icons.
A smaller selection of icons were selected to be used in testing to prioritize functionality.
The remaining icons are provided in the file labeled Val_icons. Additionally, files in Val_icons will need to be converted to png format. 
Moving forward, I'd recommend creating a py file similar to add_agent for utilities. Add_agent was created to simplify the implementation 
of new agents added. With each agent possessing numerous abilities, it would prove more tedious to implement going forward.

Secondly, the CoCo files of Agents and Utils are separate. This allows the information of neither to clutter the other. 
The hope is that they can eventually run concurrently because of this. However, as of now, the Neural Network doesn't
recognize the save of utilities_rcnn.onnx . This is because it is not copied over to release and remains in the model
where it is not visible. I suggest you start here when moving forward for them appearing on the map. Due to this bug.
The program was reverted to a state where everything still compiles and the model still creates all the data. When executed,
the program will display Agent movement alone.

Minor improvement for drawn lines is stated inside image_generator. It is more of a performance improvement than a
necessary one.
