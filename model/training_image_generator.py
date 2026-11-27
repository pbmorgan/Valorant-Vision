import os
from PIL import Image
import torchvision
import cv2
import random
import numpy as np
import math
import json
from add_agent import load_json

TEAM1_COLOR = (125/255, 211/255, 188/255, 1.0) # Color for team 1
TEAM2_COLOR = (183/255, 32/255, 48/255, 1.0) # Color for team 2

"""
agent_icon_path: relative path to the folder with agent icons
minimap_path: relative path to the folder with map layouts
misc_path: relative path to the folder with miscellaneous icons

*New* misc_path: relative path to the folder where utility icons are kept

agent_resize_factor: the size that the agent icons should be resized to when drawn on the minimap (tuple in form: height, width)

Note: images are kept internally in height, width, channels format for ease of use with cv2
"""

class ImageGenerator:
    
    def __init__(self,
                 agent_icon_path: str,
                 minimap_path: str,
                 misc_icon_path: str,
                 map_resize_factor=(384,384)):
        self.agent_icon_path = agent_icon_path
        self.map_layout_path = minimap_path
        self.map_resize_factor = map_resize_factor
        self.agent_resize_factor = (int(map_resize_factor[0] * 0.08), int(map_resize_factor[1] * 0.08)) 
        self.misc_icon_path = misc_icon_path

        self.agent_icons = []
        self.map_layouts = []
        self.misc_icons = []
        self.colors = [
                        (128,0,128,255),  #purple 
                        (0,255,0,255),  #green
                        (255, 191, 0, 255),  #light blue
                        (0,0,255,255) #red
                    ]

        # Takes in an image path and returns the image (as a numpy array) and the filename (without extension)
        def load_image(image_path):

            if os.path.isfile(image_path):

                full_image_path = os.path.basename(image_path)
                split = os.path.splitext(full_image_path)
                basefile = split[0]
                extension = split[1]

                if extension.lower() == ".png":
                    image = Image.open(image_path)
                    image = torchvision.transforms.ToTensor()(image).numpy()
                    return (image, basefile)
                else:
                    raise ValueError(f"Unsupported image format: {extension}")

        # Loads each image file in a folder and stores it
        def load_results(folder_path, array, resize_factor=None):
            for path in os.listdir(folder_path):
                path = os.path.join(folder_path, path)
                result = load_image(path)
                if result:
                    img, label = result
                    img = img.transpose(1, 2, 0) # Puts everything in HWC format for cv2
                    if resize_factor is not None:
                        img = cv2.resize(img, resize_factor)
                    array.append((img, label))

        load_results(self.agent_icon_path, self.agent_icons, resize_factor=self.agent_resize_factor)
        load_results(self.map_layout_path, self.map_layouts, resize_factor=self.map_resize_factor)
        load_results(self.misc_icon_path, self.misc_icons)

        #Read permanent IDs from id_to_name.json
        json_path = os.path.join(
            os.path.dirname(os.path.abspath(self.agent_icon_path)),
            "id_to_name.json",
        )
        """
        Documentation (delete)
        Scrapped the original to separate Agents and Utils going forward
        entries = load_json(json_path)

        self.class_ids = {
            entry["name"]: entry["id"]
            for entry in entries
        
        }
        """
        agent_entries = load_json("id_to_name.json")
        utility_entries = load_json("utility_id_to_name.json")

        self.agent_class_ids = {
            entry["name"]: entry["id"]
            for entry in agent_entries
        }
        #print(self.agent_class_ids) #test

        self.utility_class_ids = {
            entry["name"]: entry["id"]
            for entry in utility_entries
        }
        #print(self.utility_class_ids) #test

        # 
        loaded_names = {name for _, name in self.agent_icons}

        if loaded_names != set(self.agent_class_ids):
            missing = sorted(set(self.agent_class_ids) - loaded_names)
            unregistered = sorted(loaded_names - set(self.agent_class_ids))

            raise ValueError(
                f"Agent icons and id_to_name.json do not match. "
                f"Missing icons: {missing}; "
                f"Unregistered icons: {unregistered}"
                "Add new agents with add_agent.py."
            )
        
        """
        Documentation (delete)
        self.coco_data = {
            "images": [],
            "annotations": [],
            "categories": [
                {"id": cid, "name": name} for name, cid in self.utility_class_ids.items()
            ]
        }

        Split coco_data between agents and utilities
        """

        self.agent_coco_data = {
            "images" : [],
            "annotations" : [],
            "categories" : [
                {"id" : cid, "name" : name}
                for name, cid in self.agent_class_ids.items()
            ],
            "image_id" : 0,
            "annotation_id" : 0
        }

        self.utility_coco_data = {
            "images": [],
            "annotations": [],
            "categories": [
                {"id": cid, "name": name} 
                for name, cid in self.utility_class_ids.items()
            ],
            "image_id" : 0,
            "annotation_id" : 0
        }

        """
        Documentation (delete)
        All instances moved into coco_data to keep id #s separate between util and agent
        self.image_id = 0
        self.annotation_id = 0
        """

    # Pick a random minimap and return it as a numpy array
    def generate_map(self):
        minimap, _ = random.choice(self.map_layouts)
        minimap = minimap.copy()
        return minimap

    # Takes in a minimap and a number of agents to draw, and returns the minimap with the agents drawn on it, as well as the positions of the agents
    def draw_agents(self, minimap):

        if len(self.agent_icons) < 5:
            raise ValueError("Not enough agent icons to draw. At least 5 are required.")

        # Select 2 independent, unique five-agent teams.
        #The same agent can appear on both teams.
        team1_roster = random.sample(self.agent_icons, 5)
        team2_roster = random.sample(self.agent_icons, 5)

        # Round scenarios
        scenario = random.choices(
            ["early_round", "mid_round", "late_round"],
            weights=[50, 30, 20],
            k=1
        )[0]

        if scenario == "early_round":
            team1_count = 5
            team2_count = 5
        elif scenario == "mid_round":
            team1_count = random.randint(3, 5)
            team2_count = random.randint(3, 5)
        else:  # late_round
            team1_count = random.randint(1, 5)
            team2_count = random.randint(1, 5)

        selected = [
            (icon, name, "team1", TEAM1_COLOR)
            for icon, name in random.sample(
                team1_roster, team1_count
            )
        ] + [
            (icon, name, "team2", TEAM2_COLOR)
            for icon, name in random.sample(
                team2_roster, team2_count
            )
        ]

        # Randomize the drawing order so that agents are not drawn in a predictable sequence
        random.shuffle(selected)

        map_h, map_w = minimap.shape[:2]

        # Generator expects RGBA minimaps.
        playable_mask = minimap[:, :, 3] > 0.05
        playable_points = np.argwhere(playable_mask)

        # If there are no playable points, return the original minimap and an empty list of positions
        if not len(playable_points):
            return minimap, []

        position_data = []
        centers = [] # (center_x, center_y, radius)

        for source_icon, name, team, color in selected:
            # Assign team color to the agent icon
            icon, inner_r, _ = self.square_to_circle_with_border(
                image=source_icon,
                border_color=color
            )

            # Apply transformations to the selected agent icons
            if random.random() < 0.25:
                icon = self.rotate_image_randomly(
                    icon, angle_range=(-5, 5)
                )

            if random.random() < 0.1:
                icon = self.zero_out_random_angle(icon)

            old_h, old_w, = icon.shape[:2]

            scale = random.uniform(0.9, 1.1)
            new_h = max(
                8, round(self.agent_resize_factor[0] * scale)
            )
            new_w = max(
                8, round(self.agent_resize_factor[1] * scale)
            )

            icon = cv2.resize(icon, (new_w, new_h))

            # Calculate icon bounding box dimensions
            r_x = max(1, round(inner_r * new_w / old_w))
            r_y = max(1, round(inner_r * new_h / old_h))
            radius = max(r_x, r_y)

            # Make 15% of agents overlap with another agent
            wants_overlap = (
                bool(centers) and random.random() < 0.15
            )

            # Try to place the agent icon on the minimap, retrying up to 20 times if necessary 
            for attempt in range(20):
                overlap_index = None

                if wants_overlap and attempt < 10:
                    overlap_index = random.randrange(len(centers))

                    # Get the center coordinates and radius of the overlapping agent
                    overlap_x, overlap_y, overlap_r = centers[overlap_index]

                    angle = random.uniform(0, 2 * math.pi)
                    separation = random.uniform(0.65, 0.85) * (radius + overlap_r)
                    center_x = round(overlap_x + separation * math.cos(angle))
                    center_y = round(overlap_y + separation * math.sin(angle))

                else:
                    center_y, center_x = map(
                        int, playable_points[random.randrange(len(playable_points))]
                    )

                x = center_x - new_w // 2
                y = center_y - new_h // 2

                # Ensure the agent icon stays within the playable area
                if (
                    x < 0 or y < 0
                    or x + new_w > map_w
                    or y + new_h > map_h
                ):
                    continue

                # Avoid placing icons outside the playable area
                if (
                    playable_mask[y:y + new_h, x:x + new_w].mean() < 0.6
                ):
                    continue

                # Avoids accidental overlap with other agents
                if any(
                    math.hypot(center_x - cx, center_y - cy)
                    < radius + previous_radius + 3

                    for i, (cx, cy, previous_radius) in enumerate(centers)
                    if i != overlap_index
                ):
                    continue

                self.overlay_transparent(minimap, icon, x, y)

                position_data.append({
                    "name": name,
                    "team": team,
                    "coordinates": (center_x - r_x, center_y - r_y),
                    "width": 2 * r_x,
                    "height": 2 * r_y
                })

                centers.append((center_x, center_y, radius))
                break

        """
        # [DEBUGGING] Print a summary of the scenario, selected agents, and placed agents
        print(
                    f"Scenario: {scenario} | "
                    f"Selected: {len(selected)} | "
                    f"Placed: {len(position_data)}"
                )
        """
        return minimap, position_data

    # Draws miscellaneous icons on the map that just need to be drawn on (we don't need to handle them specially)
    #Repurposed for ability icons
    def draw_misc(self, minimap, num_misc_to_draw):
        misc_data = []
        map_h, map_w = minimap.shape[:2]
        overlay = minimap.copy()


        for _ in range(num_misc_to_draw):
            
            icon, label = random.choice(self.misc_icons)

            #Rotation
            #icon = self.rotate_image_randomly(icon) Disabled for fixed rotation

            #Team color ring
            color = random.choice([TEAM1_COLOR, TEAM2_COLOR])
            icon = self.circ(icon.copy(), color)

            #size
            scaling_factor = random.uniform(0.9, 1.1)
            size = int(16 * scaling_factor)
            icon_height, icon_width = icon.shape[:2]


            valid_points = np.argwhere(minimap[:, :, 3] > 0)

            if len(valid_points) == 0:
                continue

            y, x = random.choice(valid_points)

            # keep icon inside map
            x = np.clip(x, 0, map_w - size)
            y = np.clip(y, 0, map_h - size)


            self.overlay_transparent(minimap, icon, x, y, overlay_size = (size, size))

            misc_data.append({
                "name" : label,
                "coordinates" : (x, y),
                "width" : size,
                "height" : size
            })
        return minimap, misc_data

    #help construct circle around ability icons
    def circ(self, icon, color):
        icon = icon.copy()

        height, width = icon.shape[:2]

        background = np.zeros_like(icon)

        radius = min(height, width) // 2 - 2

        cv2.circle(background, (width//2, height//2), radius, color, -1)

        mask = icon[:, :, 3] > 0
        background[mask] = icon[mask]

        return background

#Drawn abiliities (Smoke, Veil, Wall, M-Wall, Fstlane)
#Note: OpenCV uses (B,G,R,A) for colors vs the traditional (R,G,B)
    def draw_smoke(self, minimap, num_smokes):
       
        map_h, map_w = minimap.shape[:2]  #draw_agents
        
        overlay = minimap.copy()
        S_position_data = []
        for i in range(num_smokes):
            center_x = random.randint(40, map_w - 40)
            center_y = random.randint(40, map_h - 40)

            radius = random.randint(10,15) #Size of Smoke generated

            color = (0,0,0, 230)
            color2 = (0,0,0, 255)


            cv2.circle(overlay, (center_x, center_y), radius, color, -1)
            cv2.circle(overlay, (center_x, center_y), radius, color2, 2)
            #print(f"Smoke {i} completed\n")
            S_position_data.append({
                "name" : "Smoke",
                "coordinates" : (center_x-radius, center_y-radius),
                "width" : 2 * radius,
                "height" : 2 * radius
            })
        

        #addWeighted used to make smokes transluscent rather than block map
        cv2.addWeighted(overlay,0.4, minimap, 0.6, 0, minimap)
        
        """
        S_position_data.append({
            "coordinates" : (center_x - radius, center_y - radius),
            "width": 2 * radius,
            "height" : 2 * radius
        })
        """

        
        return minimap, S_position_data

    def draw_veil(self, minimap, num_veils):
        overlay = minimap.copy()
        v_data = []
        A_ult = False 
        
        map_h, map_w = minimap.shape[:2]
        
        
        for i in range(num_veils):
            
            A_ult = False #reset
            x_start = random.randint(40, map_w - 40)
            y_start = random.randint(40, map_h - 40)

            angle = random.uniform(0, 2 * math.pi)
            length = random.randint(60, 170)

            x_end = int(x_start + length * math.cos(angle))
            y_end = int (y_start + length * math.sin(angle))

            x_end = np.clip(x_end, 0, map_w - 1)
            y_end = np.clip(y_end, 0, map_h - 1)

            thickness = 3 #fixed for now

            """
            Note: there is a really specific scenario on Bind where if it goes through a portal
            it will come out on the other side and continue with the same angle.
            This will also apply to M_Walls

            Solution, detect if an entrance portal is intersected (going towards the portal) subtract the intersection
            point from the x-postion of the starting to calculate the distance. The absolute value of the difference
            should be the length of the new called veil to be drawn. Additionally, the original one should be the remaining
            value that adds up to equal the total distance. I.e. if a portal is intersected at 30 of a line with length 80,
            the first line is a length of 30 and the second line is the remaining 50. If for some godforsaken reason it hits another portal,
            recursion will happen again using the 50 line and so on. This test will have to come before the line is actuall drawn
            """
            color = random.choice(self.colors)

            if (color == self.colors[0]): #Checks for Astra Ult
                #triggers both ways as it puts a wall across the map
                length = 2000 #Large number to ensure it crosses screen
                x_end = int(x_start + length * math.cos(angle))
                y_end = int (y_start + length * math.sin(angle))

                x_end2 = int(x_start - length * math.cos(angle))
                y_end2 = int (y_start - length * math.sin(angle))

                cv2.line(overlay, (x_end,y_end), (x_end2, y_end2), color, thickness)
                A_ult = True
            else:
                #default outcome, regular line
                cv2.line(overlay, (x_start,y_start), (x_end, y_end), color, thickness)

            if (not A_ult):
                x_min = min(x_start, x_end)
                y_min = min(y_start, y_end)

                width = abs(x_start - x_end)
                height = abs(y_start - y_end)
                v_data.append({
                    "name" : "Veil",
                    "coordinates" : (x_min, y_min),
                    "width" : max(width, thickness),
                    "height" : max(height, thickness)
                })
            else:
                x_min = min(x_end2, x_end)
                x_max = max(x_end, x_end2)
                y_min = min(y_end2, y_end)
                y_max = max(y_end, y_end2)
                
                width = x_max - x_min
                height = y_max - y_min
                v_data.append({
                    "name" : "CosmicDivide",
                    "coordinates" : (x_min, y_min),
                    "width" : max(width, thickness),
                    "height" : max(height, thickness)
                })
            

        return overlay, v_data

    def draw_wall(self, minimap, num_walls):
        overlay = minimap.copy()
        w_data = []

        map_h, map_w = minimap.shape[:2]

        #print(f"Generating {num_walls} walls\n")
        for i in range(num_walls):

            #Dimensions
            length = random.randint(50, 140)
            thickness = random.randint(15, 20)


            #Random Coordinate on map
            x = random.randint(40, map_w - 40)
            y = random.randint(40, map_h - 40)

            #Assign color (circle back to for team assignment)
            color = random.choice([self.colors[3], self.colors[2]])

            #Assign a rotation angle 
            angle = random.uniform(0,360) #Favored over randint for higher precssion

            box = ((x,y), (length,thickness), angle)

            corners = cv2.boxPoints(box)
            corners = np.int32(corners)

            #Draw filled wall
            cv2.fillPoly(overlay, [corners], color)

            #Bounding to satisfy COCO
            x_min = int(np.min(corners[:, 0]))
            x_max = int(np.max(corners[:, 0]))
            y_min = int(np.min(corners[:, 1]))
            y_max = int(np.max(corners[:, 1]))


            width = x_max - x_min
            height = y_max - y_min


            w_data.append({
                "name" : "Wall",
                "coordinates" : (x_min,y_min),
                "width" :width,
                "height" : height
            })

        return overlay, w_data

    def draw_mwall(self, minimap, num_mwalls):
        overlay = minimap.copy()
        mw_data = []

        
            
        map_h, map_w = minimap.shape[:2]
        for i in range(num_mwalls):
            segments = [] #points to connect the wall
            #controls how drastic shifts between segments are
            #High = Harbor Low = Phoenix
            aggression = random.choice((15, 35)) 


            x = random.randint(40, map_w - 40)
            y = random.randint(40, map_h - 40)
            segments.append((x,y)) #starting point
            num_seg = random.randint(4, 10) #5 - 11 segments total

            #intial direction selection
            dx = random.randint(-10,10)
            dy = random.randint(-10,10)

            for i in range(num_seg):
                dx += random.uniform(aggression * -1, aggression)
                dx = np.clip(dx, -40, 40)
                dy += random.uniform(aggression * -1, aggression)
                dy = np.clip(dy, -40, 40)


                x += dx
                y += dy

                x = np.clip(x, 0, map_w - 1)
                y = np.clip(y, 0, map_h -1)

                segments.append((x,y))

            thickness = 3

            #Team color assignment
            color = random.choice([self.colors[3], self.colors[2]])

            
            cv2.polylines(overlay, [np.array(segments, dtype=np.int32)], False, color, thickness)
            x_segs = [segment[0] for segment in segments]
            y_segs = [segment[1] for segment in segments]

            x_min = min(x_segs)
            x_max = max(x_segs)
            y_min = min(y_segs)
            y_max = max(y_segs)


            name = "HighTide" if (aggression == 35 or num_seg >= 8) else random.choice(("HighTide", "Blaze")) #Possibility of Harbor ending his early and is simple

            mw_data.append({
                "name" : name,
                "coordinates" : (x_min, y_min),
                "width" : (x_max - x_min),
                "height" : (y_max - y_min)
            })

        return overlay, mw_data

    def draw_fstlane(self, minimap, num_lanes): #Neon(parellel lines)
        overlay = minimap.copy()
        f_data = []
        offset = 10
        length = 50 #fixed length
        hl = length / 2 #simpler
        thickness = 3

        map_h, map_w = minimap.shape[:2]
        #print(f"Generating {num_lanes} lanes\n")

        for i in range(num_lanes):
            x_center = random.randint(40, map_w - 40)
            y_center = random.randint(40, map_h - 40)

            angle = math.radians(random.uniform(0, 360))
            #Assign color (circle back to for team assignment)
            color = random.choice([self.colors[3], self.colors[2]])

            #used to find the parrallel points for the lanes
            dx = math.cos(angle)
            dy = math.sin(angle)
            px = -dy
            py = dx 
             #spacing between the lanes

            x1 = x_center + px * offset
            y1 = y_center + py * offset

            x2 = x_center - px * offset
            y2 = y_center - py * offset

            

            #Start and end points of lanes
            s1 = (int(x1 - dx * hl), int(y1 - dy * hl))
            e1 = (int(x1 + dx * hl), int(y1 + dy * hl))

            s2 = (int(x2 - dx * hl), int(y2 - dy * hl))
            e2 = (int(x2 + dx * hl), int(y2 + dy * hl))

            cv2.line(overlay, s1, e1, color, thickness)
            cv2.line(overlay, s2, e2, color, thickness)

            #grouping for cordinates
            x_points = [s1[0], e1[0], s2[0], e2[0]]
            y_points = [s1[1], e1[1], s2[1], e2[1]]

            x_min = min(x_points)
            x_max = max(x_points)
            y_min = min(y_points)
            y_max = max(y_points)

            f_data.append({
                "name": "FstLane",
                "coordinates": (x_min, y_min),
                "width": x_max - x_min,
                "height": y_max - y_min
            })            


        return overlay, f_data


    def draw_utils(self, minimap): #Draw and misc(icons) functions to draw all utils in one place (cleans up generate_data())
        utils = []
        
        #Modifiers to control how many of each are created
        smokes = random.randint(1, 5)
        veils = random.randint(1, 4)
        walls = random.randint(0, 3)
        mwalls = random.randint(0, 3)
        fstlanes = random.randint(2,4)
        miscs = random.randint(3, 12)


        minimap, smoke_data = self.draw_smoke(minimap, smokes)
        utils.extend(smoke_data)
            
        minimap, veil_data = self.draw_veil(minimap, veils)
        utils.extend(veil_data)
            
        minimap, wall_data = self.draw_wall(minimap, walls)
        utils.extend(wall_data)
                  
        minimap, mwall_data = self.draw_mwall(minimap, mwalls)
        utils.extend(mwall_data)

        minimap, lanes_data = self.draw_fstlane(minimap, fstlanes)
        utils.extend(lanes_data)
        
        minimap, icons = self.draw_misc(minimap, miscs)
        utils.extend(icons)


        return minimap, utils

    def create_circular_mask(h, w):

        center = (int(w/2), int(h/2))
        
        radius = min(center[0], center[1], w-center[0], h-center[1])

        Y, X = np.ogrid[:h, :w]
        dist_from_center = np.sqrt((X - center[0])**2 + (Y-center[1])**2)

        mask = dist_from_center <= radius

        return mask, center, radius

    # Takes in a numpy array (image) and returns a circular version with a random array coming out of it as a numpy array
    def square_to_circle_with_border(self, image: np.ndarray, border_color=None,
                                      direction_angle: float = None) -> np.ndarray:  # noqa: keep sig
        """
        Renders a Valorant-style agent indicator: a circular icon with a coloured
        teardrop border whose point indicates facing direction.

        The teardrop is a single continuous hollow shape — a wide circle base that
        smoothly narrows to a point — matching the in-game minimap indicator.

        Expects and returns HWC RGBA float32 in [0, 1].

        Args:
            image:           HWC RGBA float32 source icon.
            border_color:    RGBA float32 tuple. Defaults to red (enemy) or teal (ally).
            direction_angle: Tip direction in degrees (0 = up, clockwise).
                             If None, a random direction is chosen.
        """
        if border_color is None:
            border_color = (183/255, 32/255, 48/255, 1.0) if random.random() < 0.5 \
                           else (125/255, 211/255, 188/255, 1.0)

        # Apply a little randomness to the border color
        COLOR_DEVIATION_FACTOR = 0.02 # Small random deviation applied to border color
        border_color = (random.uniform(-COLOR_DEVIATION_FACTOR, COLOR_DEVIATION_FACTOR) + border_color[0], 
                        random.uniform(-COLOR_DEVIATION_FACTOR, COLOR_DEVIATION_FACTOR) + border_color[1], 
                        random.uniform(-COLOR_DEVIATION_FACTOR, COLOR_DEVIATION_FACTOR) + border_color[2], 1)

        border_color = np.clip(border_color, 0, 1)

        if direction_angle is None:
            direction_angle = random.uniform(0, 360)

        # --- 1. Expand canvas to fit the tip without clipping ---
        src = image.copy()

        # fill in transparent pixels with border color
        transparent_pixels = src[:, :, 3] == 0
        src[transparent_pixels] = border_color

        h, w = src.shape[:2]
        outer_r   = min(h, w) // 2
        border_px = max(3, outer_r // 8)
        arrow_len = int(outer_r * 0.55)   # tip protrusion beyond circle edge

        pad   = arrow_len + border_px + 4
        new_h = h + 2 * pad
        new_w = w + 2 * pad

        canvas = np.zeros((new_h, new_w, 4), dtype=np.float32)
        canvas[pad:pad+h, pad:pad+w] = src
        src_padded = canvas.copy()  # icon on expanded canvas, used to restore centre

        cx = pad + w // 2
        cy = pad + h // 2


        # --- 2. Build the teardrop mask ---
        # The outer teardrop = union of:
        #   (a) the main circle  (radius outer_r)
        #   (b) a linearly tapered wedge that narrows from outer_r wide at the
        #       circle centre to 0 at the tip  (distance tip_reach from centre)
        # The inner hollow = same shape shrunk inward by border_px.
        # Ring = outer & ~inner.  Then zero alpha everywhere outside ring.
        math_angle = np.radians(-(direction_angle - 90))
        dx = np.cos(math_angle)
        dy = -np.sin(math_angle)

        tip_reach = outer_r + arrow_len   # distance from centre to tip

        Y, X = np.ogrid[:new_h, :new_w]
        Xf = X.astype(np.float32)
        Yf = Y.astype(np.float32)

        proj     = (Xf - cx) * dx + (Yf - cy) * dy          # along arrow axis
        perp_abs = np.abs((Xf - cx) * (-dy) + (Yf - cy) * dx)  # ⊥ distance
        dist_c   = np.sqrt((Xf - cx)**2 + (Yf - cy)**2)

        # Outer boundary
        taper_hw_outer = outer_r * (1.0 - proj / tip_reach)
        in_outer = (dist_c <= outer_r) | (
            (proj >= 0) & (proj <= tip_reach) & (perp_abs <= np.maximum(taper_hw_outer, 0))
        )

        # Inner boundary (border_px smaller in every direction)
        inner_r      = outer_r   - border_px
        inner_reach  = tip_reach - border_px
        taper_hw_inner = inner_r * (1.0 - proj / inner_reach)
        in_inner = (dist_c <= inner_r) | (
            (proj >= 0) & (proj <= inner_reach) & (perp_abs <= np.maximum(taper_hw_inner, 0))
        )

        ring = in_outer & ~in_inner

        # Fill entire teardrop solid, then cut a circular hole for the icon
        canvas[~in_outer, 3] = 0.0
        canvas[in_outer] = border_color
        # Restore the icon pixels inside the inner circle (the hollow centre)
        inner_circle = dist_c <= inner_r
        canvas[inner_circle] = src_padded[inner_circle]

        # --- 3. Crop back symmetrically ---
        crop_pad = arrow_len + border_px + 2
        out = canvas[pad - crop_pad : pad + h + crop_pad,
                     pad - crop_pad : pad + w + crop_pad]

        return np.ascontiguousarray(out), inner_r, crop_pad

    # Takes in a background and overlays an image on top of it (the overlay must be RGBA). X and Y are the position the overlay should appear on the background
    def overlay_transparent(self, background, overlay, x, y, overlay_size=None):
        """
        @brief      Overlays a transparant PNG onto another image using CV2

        @param      background_img    The background image
        @param      img_to_overlay_t  The transparent image to overlay (has alpha channel)
        @param      x                 x location to place the top-left corner of our overlay
        @param      y                 y location to place the top-left corner of our overlay
        @param      overlay_size      The size to scale our overlay to (tuple), no scaling if None

        @return     Background image with overlay on top
        """

        overlay = cv2.cvtColor(overlay, cv2.COLOR_RGBA2BGRA) # Convert RGBA to BGRA
        overlay = cv2.resize(overlay, overlay_size) if overlay_size is not None else overlay

        background_width = background.shape[1]
        background_height = background.shape[0]

        if x >= background_width or y >= background_height:
            return None

        h, w = overlay.shape[0], overlay.shape[1]
        if h == 0 or w == 0:
            return None

        if x + w > background_width:
            w = background_width - x
            overlay = overlay[:, :w]

        if y + h > background_height:
            h = background_height - y
            overlay = overlay[:h]

        if overlay.shape[2] < 4:
            overlay = np.concatenate(
                [
                    overlay,
                    np.ones((overlay.shape[0], overlay.shape[1], 1), dtype=overlay.dtype)
                ],
                axis=2,
            )
        overlay_image = overlay[..., :4]
        mask = overlay[..., 3:]

        #print(background.shape, overlay.shape, overlay_image.shape, y, y+h, x, x+h)
        x = max(x, 0)
        y = max(y, 0)
        background[y:y+h, x:x+w] = (1.0 - mask) * background[y:y+h, x:x+w] + mask * overlay_image

        return background

    def rotate_image_randomly(self, image, angle_range=(0, 360)):
        height, width = image.shape[:2]
        center = (width // 2, height // 2)
        # Generate random angle, range can be adjusted for augmentation
        angle = random.uniform(*angle_range)
        
        # Get the 2D rotation matrix
        rotation_matrix = cv2.getRotationMatrix2D(center, angle, 1.0)

        # Perform the affine warp to apply the rotation
        return cv2.warpAffine(image, rotation_matrix, (width, height))
   
    def apply_jpeg_compression(self, image, quality_range=(20, 75)):
        quality = random.randint(*quality_range)

        encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), quality]
        _, encimg = cv2.imencode(".jpg", (image * 255).astype(np.uint8), encode_param)

        decimg = cv2.imdecode(encimg, cv2.IMREAD_UNCHANGED)
        return decimg.astype(np.float32) / 255.0

    def add_gaussian_noise(self, image, sigma_range=(0.002, 0.01)):
        sigma = random.uniform(*sigma_range)
        noise = np.random.normal(0, sigma, image.shape)
        noisy = np.clip(image + noise, 0, 1)
        return noisy

    def scale_artifact(self, image, scale_range=(0.5, 0.9)):
        scale = random.uniform(*scale_range)

        h, w = image.shape[:2]
        down = cv2.resize(image, (int(w*scale), int(h*scale)))
        up = cv2.resize(down, (w, h), interpolation=cv2.INTER_LINEAR)

        return up

    def skew_image(self, image, skew_angle_deg_x=0, skew_angle_deg_y=0):
        # Convert angles to radians and calculate tangent
        # The value is in radians, so use math.tan()
        sx = math.tan(math.radians(skew_angle_deg_x))
        sy = math.tan(math.radians(skew_angle_deg_y))

        h, w = image.shape[:2]

        # Create the 2x3 shear matrix for cv2.warpAffine
        # M = [[1, sx, 0],
        #      [sy, 1, 0]]
        # This matrix applies shear around the origin (top-left corner)
        M = np.float32([[1, sx, 0],
                        [sy, 1, 0]])

        # Calculate new image dimensions to fit the whole skewed image
        # This might need more complex logic for perfect fitting, but for a simple case:
        new_w = int(w + abs(h * sx)) if sx != 0 else w
        new_h = int(h + abs(w * sy)) if sy != 0 else h

        # Apply the affine transformation
        skewed_image = cv2.warpAffine(image, M, (new_w, new_h))

        return skewed_image

    def make_random_invisible(self, image, percent_invisible=0.8):
        mask = np.random.rand(image.shape[0], image.shape[1]) < percent_invisible
        image[mask] = 0
        return image

    def zero_out_random_angle(self, image):

        start_angle = random.randint(0, 360)
        angle_difference = random.randint(0, 270)
        end_angle = start_angle + angle_difference

        h, w, _ = image.shape
        mask = np.zeros((h, w), dtype=np.uint8)
        cv2.ellipse(mask, (h//2, w//2), (h//2, w//2), 0, start_angle, end_angle, 255, -1)
        mask = ~mask

        image = cv2.bitwise_and(image, image, mask=mask)

        return image

        num = random.randint(1, 4)

        if num == 1:
            image[0:h//2, 0:w//2] = 0
        elif num == 2:
            image[0:h//2, w//2:w] = 0
        elif num == 3:
            image[h//2:h, 0:w//2] = 0
        elif num == 4:
            image[h//2:h, w//2:w] = 0
        
        return image

    # Takes in a background image (should be a minimap) and draws bounding boxes based on the passed in positions. Used for manually checking if boxes are right
    def draw_boxes_on_image(self, background, positions):

        for position in positions:
            x, y = position["coordinates"]
            width = position["width"]
            height = position["height"]

            top_left = (x, y)
            bottom_right = (x + width, y + height)

            # This shouldn't be needed, but y'know.
            if top_left[0] < 0 or top_left[1] < 0 or bottom_right[0] > background.shape[1] or bottom_right[1] > background.shape[0]:
                continue

            purple = (128, 0, 128)
            cv2.rectangle(background, top_left, bottom_right, purple, 2)

        return background

    # Takes in an image and a list of positions (as well as the file name) and creates the image file and saves the position data to later make the COCO file
    def output_image(self, image, positions, file_name, class_ids, coco_data, is_train=True):

        current_dir = os.getcwd()

        image_height = image.shape[0]
        image_width = image.shape[1]

        label_subdir = os.path.join("dataset", "labels", "train" if is_train else "val")
        image_subdir = os.path.join("dataset", "images", "train" if is_train else "val")

        # Make sure directories exist
        os.makedirs(os.path.join(current_dir, label_subdir), exist_ok=True)
        os.makedirs(os.path.join(current_dir, image_subdir), exist_ok=True)

        # Save image data 
        image_path = os.path.join(current_dir, image_subdir, file_name + ".png")
        # Ensure image values are uint8 for cv2.imwrite
        image_uint8 = (np.clip(image, 0, 1) * 255).astype(np.uint8) # Don't ask me why this part is here
        success = cv2.imwrite(image_path, image_uint8)
        if not success:
            raise IOError(f"Failed to write image to {image_path}")

        # Store image data in coco format
        #self.image_id += 1
        coco_data["image_id"] += 1 
        image_entry = {
            "id": coco_data["image_id"],
            "file_name": file_name + ".png",
            "width": image_width,
            "height": image_height
        }
        coco_data["images"].append(image_entry)

        # Add data of where everything is in the image to our coco data
        for position in positions:
            class_id = class_ids[position["name"]]

            x_min = position["coordinates"][0]
            y_min = position["coordinates"][1]
            width = position["width"]
            height = position["height"]

            if width <= 0 or height <= 0:
                continue

            #self.annotation_id += 1
            coco_data["annotation_id"] += 1

            annotation = {
                "id": coco_data["annotation_id"],
                "image_id": coco_data["image_id"],
                "category_id": class_id,
                "bbox": [x_min, y_min, width, height],
                "area": width * height,
                "iscrowd": 0
            }

            coco_data["annotations"].append(annotation)


    # Dumps coco data into a file and resets all information (like what # image we're on)
    # Parameter dataset_name added to sort between Util and Agent
    def save_coco(self, coco_data, dataset_name, is_train=True,):
        
        split = "train" if is_train else "val"
        path = os.path.join("dataset",
                            f"instances_{dataset_name}_{split}.json"
                            )
        

        with open(path, "w") as f:
            json.dump(coco_data, f, indent=4, default=int)

        coco_data["images"] = []
        coco_data["annotations"] = []
        coco_data['annotation_id'] = 0
        coco_data['image_id'] = 0

    # Outputs a mapping of id to name so that we can figure out what the ids actually mean (used in the actual C++ application)
    #Edited to split assignments between Agent and Util
    def output_id_to_name_mapping(self, class_ids, filename):

        output = []

        for name, class_id in class_ids.items():
            output += [{"id": class_id, "name": name}]

        #path = "id_to_name.json"

        with open(filename, "w") as f:
            json.dump(output, f, indent=4, default=int)

    #Generates util based on a random number and allocates them across aiblity categories: walls, veils, smokes and other abilities
    #def generate_util(self, utilities_data, num_utils):


    # Takes in the name of the files you want to create and outputs an image and txt file with that name
    def generate_data(self, data_name, is_train=True):

        minimap = self.generate_map()

        #wrapper function to draw all utilities
        minimap, util = self.draw_utils(minimap) 
        
        minimap, positions = self.draw_agents(minimap)
        minimap = self.add_gaussian_noise(minimap, sigma_range=(0.0, 0.01))
        minimap = self.scale_artifact(minimap, scale_range=(0.5, 1.0))
        minimap = self.apply_jpeg_compression(minimap, quality_range=(50, 100))

        #minimap = self.make_random_invisible(minimap, percent_invisible=0.01) # This one makes everything really ugly so I have it turned off
        minimap = cv2.GaussianBlur(minimap, (3, 3), 0) if random.random() < 0.5 else minimap

        # The next two could be helpful, but you'd need to recalculate the bounding boxes and I don't want to
        #minimap = self.skew_image(minimap, skew_angle_deg_x=random.randint(-10, 10), skew_angle_deg_y=random.randint(-10, 10))
        #minimap = self.rotate_image_randomly(minimap, angle_range=(-10, 10))


        self.output_image(minimap, positions, data_name, self.agent_class_ids, self.agent_coco_data, is_train=is_train)
        self.output_image(minimap, util, data_name, self.utility_class_ids, self.utility_coco_data, is_train=is_train)
    
    # Displays the ID and name of each class
    def display_class_names(self):
        print("Class ID/Name to copy into yaml file:")
        print("names:")
        for name, class_id in self.class_ids.items():
            print(f"\t{class_id}: {name}")
  

        


        