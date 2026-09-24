# 4DMM

**3DMM Renaissance, or 4DMM as I like to call it, is an experimental continuation and major expansion of Microsoft 3D Movie Maker, built from the 3DMMEx codebase.**

The basic idea is simple: **what if 3D Movie Maker had kept developing instead of stopping in 1995?**

4DMM keeps the direct, playful workflow of the original program while extending the actual application rather than replacing it with a conventional modern DCC interface. The goal is to make substantially more capable 3D filmmaking possible without turning 3DMM into Blender, Maya, or a game engine editor.

> **Status:** active development. This repository is not a finished release. Features, file formats, UI behavior, and build requirements are still changing.

## Highlights

### Camera and animation

- Fully free movable camera
- WASD + mouse free-camera control
- Camera movement recording
- Frame-by-frame camera positioning
- Motion-tweened camera movement across arbitrary frame ranges
- Improved timeline and frame insertion workflows

### Modern rendering

- Modern OpenGL renderer integrated throughout the application
- 24-bit color instead of the original 256-color rendering path
- Per-pixel lighting/shading
- Bilinear texture filtering
- Anisotropic filtering
- Improved texture rendering at oblique viewing angles
- Modern high-resolution rendering
- Support for arbitrary display resolutions supported by the user's monitor
- Dynamic light controls including intensity, range/distance, and gradiency
- Lights can be associated with scene objects
- Multiple lighting modes selectable by scene/movie
- Real-time shadow rendering under active development
- Per-object shadow-casting controls

### Scene and object editing

- Object Groups for manipulating multiple objects together
- Objects can be made non-selectable so background scenery stops interfering with foreground editing
- Expanded object browsers for selecting by scene, frame, visibility, and other useful contexts
- Modernized object selection behavior
- 24-bit color pickers for 3D Words and shape objects
- Fast texture browser for 3D Words
- Larger Undo/Redo history and expanded undoable operations
- Modern Windows load/save dialogs and improved file handling

### Actor Studio

Actor Studio has been substantially expanded beyond the original 3DMM workflow, including:

- BODY / Part / Part Group / Object Group / EVERYTHING-level editing
- Reposition, vertical movement, pitch, yaw, and roll
- Reset Rotation
- Grow / Shrink
- Stretch / Squish
- Persistent At Rest state
- Cut / Spawn behavior across hierarchy levels
- Hierarchy-aware Undo / Redo
- Part-group-aware selection behavior
- Selection persistence across frames where an entity exists
- Expanded object-group editing tools

Actor Studio is still under active development and is expected to continue changing substantially.

### Asset and conversion work

4DMM retains compatibility with classic 3DMM assets while expanding what can be brought into the application. Modern OBJ / GLTF / FBX to VXP2 conversion tooling is under active development, including higher-quality texture handling and compatibility work for modern game-engine assets.

## Why 4DMM?

Modern 3D applications are extraordinarily capable, but that capability often comes with a large interaction and workflow burden. Original 3D Movie Maker approached the problem from the opposite direction: grab something, move it, animate it, move the camera, add sound, and make a movie.

4DMM is an attempt to continue that design philosophy without freezing the technology in 1995.

The project is not trying to turn 3DMM into a conventional professional DCC package. It is trying to find out how far the original interaction model can be pushed when given a modern renderer, modern editing tools, richer object manipulation, improved asset workflows, and another thirty years of available computing power.

## Project lineage

4DMM is derived from **3DMMEx** by Ben Stone, which itself descends from Microsoft's open-source release of the original Microsoft 3D Movie Maker source code.

- 3DMMEx: https://github.com/benstone/3dmmex
- Microsoft 3D Movie Maker source release: https://github.com/microsoft/Microsoft-3D-Movie-Maker

The modern BRender work in 4DMM also builds on the broader open-source BRender preservation and modernization work performed by the BRender community.

## Building

4DMM is currently developed primarily on Windows and the build system is still changing.

The current development branch uses:

- CMake
- MSVC
- the `x86-msvc-modern-relwithdebinfo` preset
- a vendored modern BRender 1.4 tree under `brender14/`

Build documentation will be expanded as the project approaches a public binary release. At the moment, this repository should be treated as an active development tree rather than a polished build-it-yourself distribution.

## Compatibility

Preserving the original 3DMM experience and its existing movie/asset ecosystem is an important design goal. New functionality should extend the program without unnecessarily destroying compatibility with existing 3DMM content or the directness of the original workflow.

## Current state

A large amount of 4DMM is already functional, but the project is moving quickly. Expect unfinished features, debugging instrumentation, changing UI, experimental renderer work, and the occasional piece of 1995 code discovering a new and creative way to object to the year 2026.

## License and trademarks

4DMM inherits code from upstream open-source projects. **Keep the repository's existing `LICENSE`, `THIRD_PARTY_LICENSES.txt`, attribution, and other upstream legal files intact.** Individual bundled dependencies may have their own license terms.

Microsoft, 3D Movie Maker, and related names/logos are trademarks of their respective owners. 4DMM is an independent community project and is not an official Microsoft product or endorsed by Microsoft.
