Q2PRO SDK for Visual Studio 2019
=============================

This SDK includes the following software components, installed with
vcpkg in a PowerShell console. Triplets for your target architectures
are required.

git clone https://github.com/microsoft/vcpkg.git
.\bootstrap-vcpkg.bat

.\vcpkg.exe install curl:x64-windows curl:x86-windows

.\vcpkg.exe install openal-soft:x64-windows openal-soft:x86-windows

.\vcpkg.exe install libjpeg-turbo:x64-windows libjpeg-turbo:x86-windows

.\vcpkg.exe install libpng:x64-windows libpng:x86-windows

.\vcpkg.exe install sdl2:x64-windows sdl2:x86-windows

.\vcpkg.exe install opengl:x64-windows opengl:x86-windows

.\vcpkg.exe install openal-soft:x64-windows openal-soft:x86-windows

.\vcpkg.exe install zlib:x64-windows zlib:x86-windows


Please refer to each software component home page for exact copyright and
licensing information.

The vcpkg is maintained with the following commands.

git pull
.\vcpkg.exe update
.\vcpkg.exe upgrade --no-dry-run
.\vcpkg.exe remove --outdated

QwazyWabbit 2022