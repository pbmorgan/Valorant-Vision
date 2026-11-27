from training_image_generator import ImageGenerator
import os
import sys

TRAIN_IMAGES_TO_MAKE = 1024
INTERVAL_BETWEEN_PRINTS = 20

if len(sys.argv) == 2:
    TRAIN_IMAGES_TO_MAKE = int(sys.argv[1])
else:
    print(f"Number of images not specified, defaulting to {TRAIN_IMAGES_TO_MAKE}")

VAL_IMAGES_TO_MAKE = int(TRAIN_IMAGES_TO_MAKE / 4)

if __name__ == "__main__":

    current_dir = os.getcwd()
    generator = ImageGenerator(agent_icon_path=current_dir+"/agent_icons", 
                            minimap_path=current_dir+"/map_layouts",
                            misc_icon_path=current_dir+"/utility_icons_training")

    print("Starting training image generation...")

    for i in range(TRAIN_IMAGES_TO_MAKE):

        if i % INTERVAL_BETWEEN_PRINTS == 0:
            print(f"\r{i}/{TRAIN_IMAGES_TO_MAKE} training images generated")

        data_name = f"{i:04d}"
        generator.generate_data(data_name, is_train=True)

    generator.save_coco(generator.agent_coco_data, 'agents', is_train=True)
    generator.save_coco(generator.utility_coco_data, 'utilities', is_train=True)

    print("Finished generating training images")
    print("Starting validation image generation...")

    for i in range(VAL_IMAGES_TO_MAKE):

        if i % INTERVAL_BETWEEN_PRINTS == 0:
            print(f"\r{i}/{VAL_IMAGES_TO_MAKE} validation images generated")

        data_name = f"{i:04d}"
        generator.generate_data(data_name, is_train=False)

    print("Finished generating validation images")
    
    print("Creating COCO file...")
    generator.save_coco(generator.agent_coco_data, 'agents', is_train=False)
    generator.save_coco(generator.utility_coco_data, 'utilities', is_train=False)
    print("COCO file created")
    # Mapping files are versioned; don't overwrite them during generation.
    # See model/id_to_name.json and model/utility_id_to_name.json.
    print("Finished :)")


    #generator.display_class_names()    