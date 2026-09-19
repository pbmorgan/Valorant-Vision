# Valorant-Vision

# Fork Contributors
- Jackson Johnson
- Preston Morgan
- Jorgan Petit
- Luisa Quintero Pineda
- Diyar Tahir

# FOR LINUX (Debian/Ubuntu)

## Requirements
- An editor such as VSCodium
- CMake (Source Distribution)

  `https://cmake.org/download/`
- OpenCV

  `sudo apt update`

  `sudo apt install build-essential cmake libopencv-dev`
- QT

  `sudo apt install qt6-base-dev qt6-declarative-dev qt6-multimedia-dev libqt6multimediawidgets6`

## Running Software

- Make sure you are in the Valorant Vision Folder Directory
  
1. `cmake ..`

2. `make -j$(nproc)`

3. `./Valorant-Vision`
  
  

---
# FOR WINDOWS

## Requirements

### Windows Build Requirements
- Visual Studio 2022 (Community Edition) 
  - Install the following workloads:
    - Python development
    - Desktop development with C++
  
- Qt: https://www.qt.io/development/download-qt-installer-oss
  - Download the Qt installer and run it
     - Select 'Qt 6.11.2 for desktop development' AND 'Custom Installation'
     - Ensure the following components are selected:
        - Qt 6.11.2
        - MSVC 2022 64-bit
        - Additional libraries
  - Example installation path:  
    `C:\Qt\6.11.2\msvc2022_64`

- OpenCV: https://opencv.org/releases/
  - OpenCV 4.14.0 (Note: 5.x.x is not supported at this time)
  - Example path:  
    `C:\opencv\build\bin`
 
- Cmake Installed

## Environment Variables

## PATH Additions (User Variables)
Press Windows Key and search `edit environment variables for your account`
![Example of Env Menu search](images/env.jpg)

Select `Environment Varaibles` option below Startup and Recovery.


Add the following paths to the highlighted option in the image:
![Example of Env Variablle](images/picture.jpg)

AFTER DOUBLE CLICKING ON Path!! //Note: If you installed Qt or OpenCV somewhere else, use that path instead. 
`C:\Qt\6.11.2\msvc2022_64\bin`  
`C:\opencv\build\bin`
`C:\[FILE PATH TO Valorant-Vision]\Valorant-Vision\build\vcpkg_installed\x64-windows\bin` //Note: I'll find a better way to do this later, but for now this is how I got it to work.

Create new environment variable:
  - Variable name: `QT_PLUGIN_PATH`
  - Variable value (Example): `C:\Qt\6.11.2\msvc2022_64\plugins` //Note: If you installed Qt somewhere else, use that path instead.

Click 'New...' under 'System Variables':
![Example of System Variable](images/system_var.jpg)

Create new system variable:
  - Variable name: `VCPKG_ROOT`
  - Variable value (Example): `C:\vcpkg` //Note: If you installed vcpkg somewhere else, use that path instead.
![Example of System Variable](images/sys_var_example.jpg)

## To make the Build
In the Visual Studio 'Developer Powershell', make sure you're in the `\Valorant_Vision` directory and run the follwing commands:  
`cmake -S . -B build` - Generates the build files in the `\build` directory 
`cmake --build build --config Release` - Compiles the project and creates the executable in the `\build\Release` directory

If you need to rebuild or make the build again; before building run the following command in the `Developer Command Prompt` to delete the previous build:  
`rmdir /s /q build`

## Running Valorant-Vision.exe
Open the `\build\Release` directory in File Explorer and double click on `Valorant-Vision.exe` to run the program.


# For MacOS

## Requirements


