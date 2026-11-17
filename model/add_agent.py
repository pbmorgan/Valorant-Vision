"""Add new agents to Valorant-Vision's existing model (Python 3.9+).

Place beside generate.py and id_to_name.json.

NOTE: NewAgent is a placeholder for the actual agent name. Replace it with the name of the new agent you want to add!
First, place two PNGs named NewAgent_icon.png in both existing icon folders (one in agent_icons and one in agent_icons_round).
Then run the script to add the new agent:
  python model/add_agent.py --name NewAgent

Project paths are relative to this script. 
Don't try to add agents while generating a dataset ._.
"""

import argparse
from io import BytesIO
import json
import os
from pathlib import Path
import re
import sys
import tempfile

MODEL_DIR = Path(__file__).resolve().parent

# Load and validate the JSON file containing agent data.
def load_json(path=None):
    path = Path(path) if path is not None else MODEL_DIR / "id_to_name.json"
    entries = json.loads(path.read_text(encoding="utf-8"))

    # Check that the loaded JSON data is a non-empty list of agents.
    if not isinstance(entries, list) or not entries:
        raise ValueError(f"Expected a non-empty list of agents in {path}.")
    names = set()

    # Validate each agent entry in the list: ensure it is a dictionary, has a sequential ID, and a valid, unique name.
    for expected_id, entry in enumerate(entries, start=1): # Entries are set up to start from 1
        if not isinstance(entry, dict):
            raise ValueError("Each entry must be a dictionary.")
        if type(entry.get("id")) is not int or entry["id"] != expected_id:
            raise ValueError("Keep the IDs sequential starting from 1 since the c++ loader indexes icons by list position.")
        name = entry.get("name")
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", name):
            raise ValueError(f"Agent names must consist of letters, numbers, underscores, or hyphens. Invalid name: {name!r}")
        if name.casefold() in names:
            raise ValueError(f"Duplicate agent name: {name}")
        names.add(name.casefold())
    return entries

# Read and validate an icon file, verify it is in PNG format and optionally in RGBA mode.
def read_icon(path, require_rgba=False):
    try:
        from PIL import Image
    except ImportError as exc:
        raise ValueError("Pillow is required to read icons: python -m pip install Pillow") from exc
    with Image.open(path) as image:
        if image.format != "PNG":
            raise ValueError(f"Icon must be in PNG format: {path}")
        image.load()  # Check that the image is fully loaded before checking its mode
        if require_rgba and image.mode != "RGBA":
            raise ValueError(f"Icon must be in RGBA mode: {path}")
        rgba = image.convert("RGBA")
        if rgba.getchannel("A").getbbox() is None:
            raise ValueError(f"Icon is fully transparent: {path}")
        buffer = BytesIO() # Create a temporary buffer to hold the RGBA image data
        rgba.save(buffer, format="PNG")
        return buffer.getvalue()

def atomic_write(path, content):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(content)
            stream.flush()             # Verify all data is written to the temporary file before renaming
            os.fsync(stream.fileno())  # Verify data is physically written to disk
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True) # Clean up the temporary file if it still exists

# 
def add_agent(args):
    model_dir = args.model_dir.resolve()
    json_path = model_dir / "id_to_name.json"
    for folder in ("agent_icons", "agent_icons_round"):
        if not (model_dir / folder).is_dir():
            raise ValueError(f"Missing existing project folder: {model_dir / folder}")
        
    # Create a lock file to prevent concurrent modifications to the model directory.
    lock_path = model_dir / ".add_agent.lock"
    lock = None
    if not args.dry_run:
        try:
            lock = lock_path.open("x")  # Create the lock file exclusively
        except FileExistsError as exc:
            raise ValueError(f"Another registration may be running (.add_agent.lock exists)") from exc

    try:
        entries = load_json(json_path)
        name = args.name.strip()

        # Verify that the provided name is valid and does not end with "_icon".
        if not re.fullmatch(r"[A-Za-z\s]*", name) or name.lower().endswith("_icon"):
            raise ValueError(f"Provide the name only, e.g. NewAgent or KAYO (not KAY/O).")
        label = name + "_icon"
        if any(e["name"].casefold() == label.casefold() for e in entries):
            raise ValueError(f"Agent is already registered: {label}")

       # Verify that all existing agents have their icons present in both folders: agent_icons and agent_icons_round.
        for folder in ("agent_icons", "agent_icons_round"):
            missing = [e["name"] for e in entries
                       if  not (model_dir / folder / (e["name"] + ".png")).is_file()]
            if missing:
                raise ValueError(f"Missing existing icons in {folder}: {', '.join(missing)}")

        # Process each folder and its corresponding source icon.
        writes = [] # Stores the destination paths & their corresponding icon data for writing to the model directory.
        for folder, source in (("agent_icons", args.icon), ("agent_icons_round", args.round_icon)):
            destination = model_dir / folder / (label + ".png")
            collisions = [p for p in destination.parent.iterdir()
                          if p.name.casefold() == destination.name.casefold() and p.name != destination.name]
            if collisions:
                raise ValueError(f"Filename differs only in capitalization: {collisions[0]}")
            if destination.exists():
                if source is not None and source.resolve() != destination.resolve():
                    raise ValueError(f"Will not overwrite existing agent icon: {destination}")
                read_icon(destination, require_rgba=True)
            elif source is None:
                raise ValueError(f"Provide an icon (must be a PNG file) for {destination}.")
            else:
                writes.append((destination, read_icon(source)))

        entry = {"id": entries[-1]["id"] + 1, "name": label}
        entries.append(entry)
        print(f"{'Would register' if args.dry_run else 'Registering'} {label} as agent {entry['id']}.")

        for folder in ("agent_icons", "agent_icons_round"):
            print(f"  {model_dir / folder / (label + '.png')}")
        if args.dry_run:
            print("Dry run: no changes will be made.")
            return

        # Commit the JSON changes after both icons have been processed.
        # Rollback only new entries on ordinary I/O errors.
        created = []
        try:
            for destination, icon_data in writes:
                atomic_write(destination, icon_data)
                created.append(destination)
            atomic_write(json_path, (json.dumps(entries, indent=2) + "\n").encode("utf-8"))
        except OSError:
            for destination in created:
                destination.unlink(missing_ok=True)
            raise # Re-raise the original OSError after cleaning up created files.
        print(f"Updated agent registry with {label} as agent {entry['id']}.")
        print("Next: run generate.py from model/, then train and export the updated model ^_^.")
    finally:
        if lock is not None:
            lock.close()
            lock_path.unlink(missing_ok=True)

# Main function to handle command-line arguments and call add_agent.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True, help="Preserve capitalization, e.g., NewAgent.")
    parser.add_argument("--icon", type=Path, help="Source a PNG file for the agent icon.")
    parser.add_argument("--round-icon", type=Path, help="Crop a PNG file for the round agent icon.")
    parser.add_argument("--model-dir", type=Path, default=MODEL_DIR)
    parser.add_argument("--dry-run", action="store_true", help="Simulate the adding an agent to the JSON file without making actual changes.")
    args = parser.parse_args()
    try:
        add_agent(args)
    except (ValueError, OSError) as exc:
        parser.exit(1, f"Error: {exc}\n")


if __name__ == "__main__":
    main()