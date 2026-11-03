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

- vcpkg
  - From the terminal application on Windows, run the following commands:
    - `cd C:\` 
      - //Note: You can install vcpkg anywhere, but I recommend C:\ for simplicity.   
    - `git clone https://github.com/microsoft/vcpkg.git`
    - `cd vcpkg`
    - `.\bootstrap-vcpkg.bat`
  - That's it! vcpkg is now installed and ready to use.
  
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

AFTER DOUBLE CLICKING ON Path!!
  - //Note: If you installed Qt or OpenCV somewhere else, use that path instead. 
  - `C:\Qt\6.11.2\msvc2022_64\bin`  
  - `C:\opencv\build\bin`
  - `C:\[FILE PATH TO Valorant-Vision]\Valorant-Vision\build\vcpkg_installed\x64-windows\bin` 
    - //Note: I'll find a better way to handle the line above later, but for now this is how I got it to work.

Create new environment variable:
  - Variable name: `QT_PLUGIN_PATH`
  - Variable value (Example): `C:\Qt\6.11.2\msvc2022_64\plugins` 
    - //Note: If you installed Qt somewhere else, use that path instead.

Click 'New...' under 'System Variables':
![Example of System Variable](images/system_var.jpg)

Create new system variable:
  - Variable name: `VCPKG_ROOT`
  - Variable value (Example): `C:\vcpkg` 
    - //Note: If you installed vcpkg somewhere else, use that path instead.
![Example of System Variable](images/sys_var_example.jpg)

## To make the Build
In Visual Studio's 'Developer Powershell'; make sure you're in the `\Valorant_Vision` directory and run the follwing commands:
  - `vcpkg new --application`: Creates a `vcpkg.json` file in the current directory
  - These commands will add the following dependencies to your `vcpkg.json` file:
    - `vcpkg add port eigen3`
    - `vcpkg add port tesseract`
    - `vcpkg add port nlohmann-json`
    - `vcpkg add port onnxruntime`

  - Close and reopen Visual Studio and wait for cmake to finish configuring the project. This will take a long time the first time around, but once it's finished you should see a message in the output window that says "Configuring done".
  - Once cmake is done configuring, you can build the project by running the following commands:
    - //Note: The next two commands for building the project will take a long time the first time around, possibly 30 minutes or more depending on your system.
    - `cmake -S . -B build`: Generates the build files in the `\build` directory
    - `cmake --build build --config Release`: Compiles the project and creates the executable in the `\build\Release` directory

If you need to rebuild or make the build again; before building run the following command in the `Developer Command Prompt` to delete the previous build:  
`rmdir /s /q build`

## Running Valorant-Vision.exe
Open the `\build\Release` directory in File Explorer and double click on `Valorant-Vision.exe` to run the program.


#For MacOS 

- Tested on Apple Silicon (arm64). Intel Macs should use the x64-osx vcpkg triplet instead of arm64-osx. 

##Requirements 
#MacOS Build Requirements 
- Xcode Command Line Tools  
- Homebrew  
- CMake  
- Git  
- Git LFS  
- Ninja  
- pkg-config  
- Autoconf
- Automake  
- Autoconf Archive  
- Libtool 
- vcpkg 

1. Check your Mac architecture: 

  uname -m 

If the output is arm64, you are using Apple Silicon. If the output is x86_64, you are using an Intel Mac. 

2. Install the Xcode Command Line Tools: 

  xcode-select –-install 

3. Verify the C++ Compiler 

  clang++ --version 

4. Install the required development tools with Homebrew: 

  brew install cmake git git-lfs pkg-config ninja autoconf automake autoconf-archive libtool 

5. Initialize Git LFS: 

  git lfs install 

6. Clone vcpkg: 

  cd ~ 
  
  git clone (https://github.com/pbmorgan/Valorant-Vision)
  
  cd vcpkg 
  
  ./bootstrap-vcpkg.sh 

7. Set the vcpkg environment variables: 
Apple Silicon: 

  export VCPKG_ROOT="$HOME/vcpkg" 

  export VCPKG_DEFAULT_TRIPLET=arm64-osx 

Intel Mac: 
  
  export VCPKG_ROOT="$HOME/vcpkg" 
  
  export VCPKG_DEFAULT_TRIPLET=x64-osx 

8. To make these variables permanent: 
Apple Silicon: 

  echo 'export VCPKG_ROOT="$HOME/vcpkg"' >> ~/.zshrc 

  echo 'export VCPKG_DEFAULT_TRIPLET=arm64-osx' >> ~/.zshrc 

  source ~/.zshrc 

Intel Mac: 
  
  echo 'export VCPKG_ROOT="$HOME/vcpkg"' >> ~/.zshrc 
  
  echo 'export VCPKG_DEFAULT_TRIPLET=x64-osx' >> ~/.zshrc 
  
  source ~/.zshrc 

9. Install the required libraries: 

  cd ~/vcpkg 

   ./vcpkg install \ 
  qtbase \ 
  qtmultimedia \ 
  opencv4 \ 
  eigen3 \ 
  tesseract \ 
  leptonica \ 
  curl \ 
  nlohmann-json \ 
  onnxruntime 

## To make the Build 
1. Clone Valorant-Vision: 

  git clone <REPOSITORY-URL> 
  
  cd Valorant-Vision 

2. Download the Git LFS files: 

  git lfs pull 

3. Configure the project with CMake: 

  cmake -S . -B build \ 
  
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" 

4. Build the project: 

  cmake --build build -j$(sysctl -n hw.ncpu) 

5. Run Valorant-Vision: 

   ./build/Valorant-Vision 

 


