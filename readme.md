![](video.gif)

# About

This is a [shallow water equations](https://en.wikipedia.org/wiki/Shallow_water_equations) solver using the [virtual pipes method](https://diglib.eg.org/server/api/core/bitstreams/47f5228c-6f1c-4afb-ab80-b98c44575bc8/content) that runs on the GPU using the WebGPU API.

Shallow water equations describe e.g. water flow over terrain. They assume that water exists in columns without gaps, and as such can be represented as simply a heightmap of water surface over some terrain heightmap. As such, it doesn't allow vertical gaps in the water volume, or vertical water flow. To make sense of the visualization, imagine looking at a terrain with water on it directly from above.

It uses wgpu-native WebGPU implementation, and SDL2 to create a window to render to.

This project is licensed under the terms of the MIT license.

# Usage

Build the project first (see instructions below), then simply run it. By default, a 256x256 empty grid is opened, and you can add water to it with the left mouse button.

On the left you'll see a bunch of settings:

* **Application settings**
    * `VSync` -- toggle vsync (the simulations runs faster with vsync off, since it does 1 simulation step per frame)
* **Simulation settings**
    * `Cells X` -- the size of simulation grid (i.e. number of cells) in X
    * `Cells Y` -- the size of simulation grid (i.e. number of cells) in Y
    * `dt` -- the time delta used for integration (note that larger values lead to instability)
    * `Gravity` -- the strength of gravity (note that larger values lead to instability)
    * `Friction` -- the fluid friction (note that larger values lead to instability)
    * `Particles` -- the number of particles simulated to visualize the fluid flow
* **Borders settings**
    * `Left` -- set the left border to be a *wall*, a *source* of fluid, a *drain* for fluid, or a *wave* generator
    * `Right` -- same for the right border
    * `Bottom` -- same for the bottom border
    * `Top` -- same for the top border
* **Interaction settings**
    * `Action` -- select mouse button action from *no action*, *adding bed* (i.e. terrain below water), *removing bed*, *adding water*, *removing water*, or *moving water* around
    * `Radius` -- select mouse action radius, in grid cells (it is visualized as a circle)
    * `Force` -- select mouse action force (larger means stronger action, e.g. more water added)
    * `Load preset` -- load one of available terrain presets (these are randomized!)
* **View settings**
    * `Show velocity` -- show velocity vectors (these are hard to see at grid sizes larger than 64)
    * `Show particles` -- show particles advected by the fluid flow

# Simulation

The simulation solves the [shallow water equations](https://en.wikipedia.org/wiki/Shallow_water_equations) using the [virtual pipes method](https://diglib.eg.org/server/api/core/bitstreams/47f5228c-6f1c-4afb-ab80-b98c44575bc8/content) with outflow scaling from [this paper](https://inria.hal.science/inria-00402079/document).

An NxN grid simulation uses four buffers:
* NxN bed (terrain) height buffer
* NxN water column height buffer
* (N+1)x(N+1) X-direction flow buffer
* (N+1)x(N+1) Y-direction flow buffer

For the flow buffers, the last (N-th) row/column (for X/Y flow, respectively) isn't used, and exists just for convenience. All buffers are stored as float32 textures, with the bed and water buffers being stored as a single `RG32F` texture. The flow textures cannot be merged into one texture due to the access paterns of the simulation (two different kernel invocation might want to modify the X and Y flow at the same texel independently).

Additionally, a velocity buffer & a particle buffer are stored, for visualization only (they don't affect the simulation in any way).

A simulation step consists of:
* Initializing the boundary flows
* Accelerating the inner (non-boundary) flows based on water surface difference in adjacent water columns
* Outflow scaling (preventing the flows from moving more water than is available in the source water column)
* Water columns advection using the computed flows
* Vomputing per-cell average velocity from the flows
* Particle respawning & advection

# Building

To build this project, you need
* [CMake](https://cmake.org)
* [SDL2](https://www.libsdl.org/) (you can probably install it via your system's package manager)
* [wgpu-native](https://github.com/gfx-rs/wgpu-native)

To install wgpu-native, download [some release archive](https://github.com/gfx-rs/wgpu-native/releases) for your platform, and unpack it somewhere. This project was built with the [v0.19.4.1](https://github.com/gfx-rs/wgpu-native/releases/tag/v0.19.4.1) release, and might not work with older versions.

Don't forget to check out submodules:
* [imgui](https://github.com/ocornut/imgui.git) for tool UI

You can do this at clone time, using `git clone <repo-url> --recurse-submodules`. Add `--shallow-submodules` to prevent loading the whole commit history of those submodules. Otherwise, you can checkout submodules at any time after cloning the repo with `git submodule update --init --recursive`.

Then, follow the usual steps for building something with CMake:
* Create a build directory
* In the build directory, run `cmake <path-to-webgpu-demo-source> -DWGPU_NATIVE_ROOT=<path-to-unpacked-wgpu-native>`
* Build the project: `cmake --build .`