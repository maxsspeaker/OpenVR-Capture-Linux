# OpenVR Capture for OBS Studio

This plugin allows capturing directly from OpenVR/SteamVR in full resolution.

A fork of OBS-OpenVR-Input-Plugin, originally made by Keijo "Kegetys" Ruotsalainen

A fork of OBS-OpenVR-Input-Plugin, originally made by Keijo "Kegetys" Ruotsalainen



### Q. What benefits does this have over the original fork?
A.
- It was rewritten using ChatGPT
- working on Linux!

---------

Installation:
1. Download latest release .zip
2. Extract all files to ~/.config/obs-studio/plugins/

---------

Compiling:
1. Pull OBS Studio source code recursively (`git clone https://github.com/obsproject/obs-studio.git --recursive`)
2. Pull this repo, copy "plugins" into the root of OBS Studio's source code (`git clone https://github.com/maxsspeaker/OpenVR-Capture-Linux`)
3. Pull OpenVR SDK inside "deps" folder. (`git clone https://github.com/ValveSoftware/openvr.git`)
4. Add `add_obs_plugin(linux-openvr PLATFORMS LINUX)` to the end of obs-studio/plugins/CMakeLists.txt
5. Compile from root directory with `cmake -S . -B build_ubuntu -G Ninja && cmake --build build_ubuntu -j$(nproc) --target linux-openvr`
