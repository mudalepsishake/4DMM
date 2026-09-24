# 4DMM

**3DMM Renaissance, or 4DMM as I like to call it, is an experimental continuation and major expansion of Microsoft 3D Movie Maker, built from the 3DMMEx codebase.**

The basic idea is simple: **what if 3D Movie Maker had kept developing instead of stopping in 1995?**

4DMM keeps the direct, accessible workflow of the original program while extending the actual application rather than replacing it with a conventional modern DCC interface. The goal is to make substantially more capable 3D filmmaking possible without turning 3DMM into Blender, Maya, or a game engine editor.

> **Status:** active development. The features below are as described but still require further integration for example with Scene Organizer capabilities as well as more stability testing. Many features are in place and ready to be finalized, other features are in development and still others are on the drawing board. File formats and UI behavior are functional but still changing so files saved with in-development versions may not be compatible with later versions. 4DMM is for 64-bit Windows operating systems.

## Highlights

### Camera and animation

- Fully free movable camera
- WASD + mouse free-camera control
- Camera movement recording
- Frame-by-frame camera positioning
- Motion-tweened camera movement across arbitrary frame ranges
- Improved timeline and frame insertion workflows
- Still to come: shaping camera movement in any axes as well as additional more sophisticated shaped-based camera movements

### Modern rendering

- Introducing BLAZE engine--4DMM's proprietary Blazing Render-based engine
- Modern OpenGL 4.3 renderer integrated throughout the application
- 24-bit color instead of the original 256-color rendering path
- Per-pixel lighting/shading
- Bilinear texture filtering
- 16x Anisotropic filtering
- Say goodbye to 3DMM's texture inconsistencies. With BLAZE, textures are a breeze
- Run the editor's movie view and play back movies--new and old--at any resolution
- Render support for your monitor's native resolution--or work utilize a dual monitor setup
- Dynamic light controls including intensity, range/distance, and gradiency
- Support for up to 48 lights per scene
- Lights can be positioned using the same tools 3DMM uses for positioning other objects
- Multiple lighting modes selectable by scene/movie
- Real-time shadow rendering
- Per-object shadow-casting controls
- Still to come: additional improvements for shadow rendering, transparent objects, water, smoke effects, and more

### Object Organization, Layering, Grouping and Quality of Life (QoL) Improvements

- Object Groups for manipulating multiple objects together
- Objects' ability to be selected can be toggled on and off so background objects don't interfere with editing
- Animate one part of a scene, then turn off selection for that part of the scene, and begin working on another part
- Ability to manipulate the currently selected object regardless of position-based clickability
- Expanded object browsers for selecting by scene, frame, visibility, and other useful contexts
- Full object browsers that are fully featured
    - Show only the objects that are currently visible by the camera's position (camera positioning can be adjusted arbitrarily) 
    - Show only the objects that are in the current scene
    - Show only the objects that are in the current frame
    - Instantly move the camera to any object in the scene by selecting it in the browser
- 24-bit color pickers for 3D Words and shape objects
- QoL texture browser for 3D Words (no more clicking a million times to get to a texture) 
- Undo/redo history window that keeps track of every click
- Load VMM files natively in-app without need for outside/external tools
- Save VMM files natively in-app without need for outside/external tools

### Actor Studio

Actor Studio has been substantially expanded the capabilities of 3DMM for directors interested in taking their creations to the next level:

- Import object groups into actor studio and create animations for them which can reused the same way as default actors' animations.
- Or, create new animations for default actors, with the ability to fully pose and manipulate their bodies frame by frame.
    - Portray actions, expressions and emotions which were never possible before
- Re-use handmade characters and the animations you mnake for them just like default actors. Fill up an entire scene with your characters.
- Full editor with the controls of 3DMM:
- Pose actors as needed to create new Actions (animations)  and stances showing what actors are doing more clearly
- Reposition, vertical movement, pitch, yaw, and roll
- Reset Rotation
- Grow / Shrink
- Stretch / Squish
- Persistent At Rest state
- Cut / Spawn behavior across hierarchy levels
- Hierarchy-aware Undo / Redo
- Use advanced selection controls to streamline movement planning and multiple object control:
    - Organize by Parts, Part Groups, Objects (Actor/Props/3D Words), Grouped Objects, and Select All
- Browse Actor's body parts by part or by part group (leg, arm, both legs, upper body, head and neck, etc) 

Actor Studio is still under active development and is expected to continue changing substantially.

### Asset and conversion work

4DMM retains compatibility with classic 3DMM assets while expanding what can be brought into the application. Modern OBJ / GLTF / FBX to VXP2 conversion tooling is under active development, including higher-quality texture handling and compatibility work for modern game-engine assets. 4DMM uses its own file format to store frame-by-frame camera positioning information, object groupings, light properties, shadow properties, and more. We will be implementing a new file format so that all of these new features will be saveable alongside the existing (and improved) features of VMM files.

## Why 4DMM?

Modern 3D applications are extraordinarily capable, but that capability often comes with a large interaction and workflow burden that can take years to master even the basic. Original 3D Movie Maker approached the problem from the opposite direction: learn while doing--grab something, move it, animate it, add sound, and make a movie. Expanding on this approach is the foundation of 4DMM.

4DMM is an attempt to continue that design philosophy without freezing the technology in 1995.

The project embraces what 3DMM already is. It takes the original to the next level in terms of enabling what was previously not possible with the software by implementing a modern renderer, sophisticated editing tools, richer object manipulation, improved asset workflows, and taking advantage of the last 30 years of technology advancement.

## Project lineage

4DMM is derived from **3DMMEx** by Ben Stone and mcayland, which itself descends from Microsoft's open-source release of the original Microsoft 3D Movie Maker source code.

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

Build documentation will be expanded as the project approaches a public binary release. Up until that moment, this repository should be treated as an active development tree rather than a polished build-it-yourself distribution.
KBD Control:


GENERAL EDITOR CONTROLS:

Frame buttons:
Ctrl+Next: duplicate after current and select the new frame.
Ctrl+Previous: duplicate before current and select the new frame.
Shift+Ctrl+Next/Previous: same placement, but the inserted frame contains no actors, props, or text boxes for that frame only.
Ctrl+Alt+Next/Previous: pops up dialog asking how many frames to insert from current frame
CTRL+S makes an object no longer selectable. double clicking on it in the browser re-enables selection. double clicking toggle selection in browsers. "(X)" before object name means it is not currently selectable
ALT+CLICK
ALT+CLICK AND DRAG this allows for applying tools to objects which are otherwise not selectable due to their current size or position in relation to other objects. once the object is currently selected, simply hold the alt key and click or click and drag any where on the screen to use the current tool on the object. 
CTRL+ALT+D delete frames in bulk from current position. similar to CTRL+ALT+Next frame or CTRL+ALT+Previous frame except deletes the number of frames you enter from the currently displayed frame.

ACTORS + PROPS TAB CONTROLS
F - enter free camera mode
ALT+Y toggles selection box color between the default yellow and the new shaded grey which shows how light is hitting the selected object
CTRL+grow/shrink tool allows for growing object beyond 10x size to 100x size
CTRL+SHIFT+L toggles between lights on and default 3dmm lights
CTRL+ALT+L toggles invisible object lights visibility
CTRL + ~  and CTRL+~ opens settings
ESC unselect the currently selected object (removes the selection box)

ACTION TOOL:
RIGHT-CLICK on an actor with the Action Tool selected opens the Actor Studio Window and Actor Studio Viewport


3D Words Window controls:
ALT+C - opens up color picker

Props Dresser Window controls:
ALT+C - opens up color picker for basic shape props (is this still CTRL+C?)

ACTOR STUDIO:
CTRL+UP ARROW / CTRL+DOWN ARROW selects the next/previous frame of the currently selected animation (action)
ALT+UP ARROW/ ALT+DOWN ARROW selects the next/previous selection (Everything/OG[s]/Actors,Props,3dwords/PGs/parts) in the body parts list
UP ARROW/DOWN ARROW moves the current selection up/down in the last list (Frames/Parts) which was clicked on
CTRL+1 selects the Position tool
CTRL+2 toggles the Up/Down checkbox for the Position tool
CTRL+Q selects the Pitch rotation tool
CTRL+W selects the Yaw rotation tool
CTRL+E selects the Roll rotation tool
CTRL+R selects the Reset rotation tool
CTRL+A selects the Grow/Shrink tool
CTRL+S selects the Stretch/Squash tool
CTRL+D selects the Grow/Shrink/Stretch/Squash Reset tool
CTRL+MOUSE1 hold down in ASV to move the camera around the object or object group(s)
MOUSEWHEEL UP/DOWN zooms in and out
MOUSE3 hold down to adjust the height of the camera
ALT+MOUSE1 hold down in ASV to use the currently selected tool on the contents of the selection box (intended to be used when a selection won't stay selected when clicking on it)
CTRL+C copies the contents of the selection box
CTRL+V pastes the contents of the selection box into the currently selected grouping in the parts list, or the nearest (hierarchically speaking) valid grouping. 

CONSIDERATIONS ON COPYING AND PASTING WITHIN ORGANIZATIONAL STRUCTURES: If you attempt to copy an actor's body part and then paste it into the OG that the actor is in, it's going to create a new actor for that part to be inserted into. 
If you attempt to copy an OG and paste it inside a PG, it will ask you which of the following you want to do:
A) Paste any and all actor(s), prop(s) and/or 3DWords contained in the OG you are pasting into the OG that the actor/prop (whose PG you are attempting to paste into) exists in
B) Take each actor/prop/3dword in the OG and translate them into PGs in the same actor/prop (whose PG you are attempting to paste into).

MANUAL CAMERA MODE
Click: enters manual camera mode [2nd toggle from the middle under scenes]
Left/Right: saves, then moves to the adjacent frame. A depth-tween frame is refused with the requested message.
E: saves the live camera and opens the editable manual-camera text window.

Ctrl+click the lit Manual Camera button: opens that same editor.
light on manual camera mode button: light on = manual camera position is specified for current frame

depth tween controls:
click on the 1st toggle from the middle under scenes: enters depth tween dialogue, allows for editing distance of tween between two frames, as well as the frame range for the tween, and the yaw angle for the tween
Invisible button: aligns depth tween yaw with longest edge of object that has long edge going away from the camera, like as if the camera was looking down a street
toggle on the far right in the OBJECTS tab: allows for synchronizing an actors movement with the camera's motion tween, or not


MULTI SELECT
shift + left click allows for selection of more than 1 object in the viewport (also works in prop browser -- add)
shift + right click de-selects an object in the viewport (also works in prop browser -- add)
CTRL+S - pressing these keys together with a single object selected makes the object non-selectable



Free Camera mode - for editing a custom scene from different camera angles - available on actors and props tab:
F: enables/disables free cam mode. pressing F while free cam mode is active returns the cam to its specified position for the current frame
Esc + Tab: exits free cam mode without resetting camera so editing props is possible from a different camera position. does not save camera position to frame.
W,A,S,D, mouse x, and mouse y: standard shooter-style movement
Shift: enables/disables the ability to change the height of the camera. disabling this after 'flying upwards' results in the free camera then being incapable of changing its y-value at the current altitude 


ARGUMENTS
To start with all features enabled, use the following arguments:
3dmovie.exe -w -e -c -a -l -u -sh -multi -resolution 900 -cursor_size 1x -gui_scale 2

DETAILED ARGUMENT INFORMATION
-c - color depth "rgb888"
-a - actor shading/lighting
-e - goes straight to editor, skips startup screen, menu screen, and mczee
-u - undo history - enables the undo history window
-w - windowed mode
-v - viewport - duplicates the viewport in an external window. viewport remains visible even when menus are open.
-t theater - goes straight to the theater (unfinished)
-o "C:\path\to\movie.3mm" - opens specified movie immediately (skips open screen) - currently configured to work for "-t" theater mode primarily but may also work for "-e" editor mode, although working with "-e" editor mode may require rewiring the old code for that pathway first
-l - light - creates a spotlight which shines directly at objects in the same direction as the camera (only affects objects with shading like 3d words and 3d shape props)
-multi - multiselect mode for objects allows for holding the shift key and selecting more than one object at a time
-logs takes logs relevant to various things and saves them in the folder that 3dmovie.exe is inside of
-uvdump creates logs specific to texture geometry for fixing texture bugs such as the taxi, cop car glitches that appear on the door lines as the prop rotates (U, V dump diagnostic - dumps specific U, V texture info about certain textures of the first loaded object to uvdump folder (creates folder in same folder as 3dmovie.exe)
-perf lightweight logs for various purposes intended to create logs with a focus on performance (less verbose logs to test performance improvements)
-precache unused precaching loader utility that was created before we started running 3dmm with optimization
-3dfix attempts to fix a y-axis issue in which objects that are significantly higher than the ground will move below the ground when the camera goes high enough
-logs_light_ed enabled the exporting of logs specific to the light labs or light editor window, and/or light properties in the 3CT files
-modern_br_log enables the exporting of logs specific to the implementation of the modern OpenGL BRender v1.4
-gui_scale changes the gui size. 2 is the default, so setting this to 1 will make the gui look more normal on some resolutions/dpi scales/systems. it supports 1 decimal point. so 1.5 is valid, 1.2 is valid. 3.2 is valid, etc.
-resolution 1080 is 1080p, 2160 is 4K. or, resolution can be scaled from the original 640x480 as follows:
-resolution 2x
-resolution 3.25x
-resolution 4.1x
etc.
-cursor_size 2x scales the cursor size up by a factor of 2. So this argument allows for adjusting the size of the cursor using scaling similar to the resolution argument

## Compatibility

Preserving the original 3DMM experience and its existing movie/asset ecosystem is an important design goal. New functionality should extend the program without unnecessarily destroying compatibility with existing 3DMM content or the directness of the original workflow.

## Current state

A large amount of 4DMM is already functional, but the project is moving quickly. Expect features in varying degrees of completeness, debugging instrumentation, changing UI, experimental renderer work, and the occasional piece of 1995 code discovering a new and creative way to object to the year 2026.

## License and trademarks

4DMM inherits code from upstream open-source projects. **Keep the repository's existing `LICENSE`, `THIRD_PARTY_LICENSES.txt`, attribution, and other upstream legal files intact.** Individual bundled dependencies may have their own license terms.

Microsoft, 3D Movie Maker, and related names/logos are trademarks of their respective owners. 4DMM is an independent community project and is not an official Microsoft product or endorsed by Microsoft.
