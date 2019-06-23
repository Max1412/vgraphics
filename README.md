# VGraphics


### Description
Project containing my hybrid ray tracing demo for my master's thesis.
It is using Vulkan + RTX (VK_NV_ray_tracing) for ray traced shadows, ambient occlusion and reflections in a rasterization pipeline.
Also contains some prototyping projects. The hybrid ray tracing executable is called *rtcombined*.

### Screenshots
![All](/resources/screenshots/all.png)
![Shadow3](/resources/screenshots/shadow3.png)
![Shadow1](/resources/screenshots/shadow1.png)
![Shadow4](/resources/screenshots/shadow4.png)

### Vulkan extensions:
* VK_NV_ray_tracing
* Standard extensions for swap chain and debugging

### Used libraries:
* [ImGui](https://github.com/ocornut/imgui)
* [stb_image](https://github.com/nothings/stb)
* [Assimp](http://assimp.org/)
* [glfw3](http://www.glfw.org/)
* [glm](https://glm.g-truc.net/0.9.8/index.html)
* [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
* [spdlog](https://github.com/gabime/spdlog)
* [glslc](https://github.com/google/shaderc)
* [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) 
* 
### Usage
#### Windows
* Please use [vcpkg](https://github.com/Microsoft/vcpkg) for dependency management when using windows to use this project
* Install assimp:x64-windows, glfw3:x64-windows, spdlog:x64-windows and glm:x64-windows using vcpkg
* Make sure the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) is installed.
* I recommend the [PICA PICA Mini Diorama](https://sketchfab.com/3d-models/pica-pica-mini-diorama-01-45e26a4ea7874c15b91bd659e656e30d) scene (as gltf). Please download and unpack it into the *resources* folder. 
* Set the correct path to your installation of vcpkg (vcpkg.cmake) in the CMakeSettings.json-file or use the included script (set_vcpkg_path.ps1) to select it
* Open the project folder in Visual Studio (2019 is recommended, 2017 works too)
  

### Executables
##### RTcombined
* Demo for my masters thesis
* Renders the scene into a G-Buffer
* Uses RTX for ray traced shadows, ambient occlusion, and reflections
* Combines ray traced results with direct lighting
* Acceleration structure can be updated
* More features: Triple buffering, PBR (Cook-Torrance BRDF), GUI for settings, shader live-reloading, dynamic light sources, included timers
##### Others
* All other executables were just for protoyping, smaller examples, and learning.

### Resources/Licensing

* PBR & IBL code adapted (with changes) from [Joey de Vries](https://twitter.com/JoeyDeVriez)' [learnopengl.com](learnopengl.com), licensed under [CC BY-NC 4.0](https://creativecommons.org/licenses/by/4.0/legalcode).