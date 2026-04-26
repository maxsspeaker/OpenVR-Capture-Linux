# OpenVR Capture for OBS Studio

This plugin allows capturing directly from OpenVR/SteamVR in full resolution.

A fork of OBS-OpenVR-Input-Plugin, originally made by Keijo "Kegetys" Ruotsalainen

A fork of OBS-OpenVR-Input-Plugin, originally made by Keijo "Kegetys" Ruotsalainen

<img width="2544" height="1425" alt="Снимок экрана_20260426_145319" src="https://github.com/user-attachments/assets/97ddd506-75df-454f-b43b-8481ba4ba2b7" />


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


PS: I needed a plugin for Linux, but no one had ported it yet, so I did it myself. If you'd like to contribute, please submit a pull request.

I would really appreciate it!
