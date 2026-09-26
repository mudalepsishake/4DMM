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

- Introducing BLAZE engine--4DMM's proprietary open-source (Blazing Render-based) engine
- Modern OpenGL 4.3 renderer integrated throughout the application
- 24-bit color instead of the original 256-color rendering path
- Per-pixel lighting/shading
- Bilinear texture filtering
- 16x Anisotropic filtering
- Say goodbye to 3DMM's texture inconsistencies. With BLAZE, textures are a breeze
- Run the editor's movie view and play back movies--new and old--at any resolution
- Render support for your monitor's native resolution--or utilize a dual-monitor setup
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

Actor Studio substantially expands the capabilities of 3DMM for directors interested in taking their creations to the next level:

- Import object groups into actor studio and create animations for them which can be reused the same way as default actors' animations.
- Or, create new animations for default actors, with the ability to fully pose and manipulate their bodies frame by frame.
    - Portray actions, expressions and emotions which were never possible before
- Re-use handmade characters and the animations you make for them just like default actors. Fill up an entire scene with your characters.
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

4DMM retains compatibility with classic 3DMM assets while expanding what can be brought into the application. Modern OBJ / GLTF / FBX to VXP2 conversion tooling is under active development, including higher-quality texture handling and compatibility work for modern game-engine assets. 4DMM uses its own file format (currently a .3ct sidecar file) to store frame-by-frame camera positioning information, object groupings, light properties, shadow properties, and more. In the release candidate, we'll be implementing a new file format so that all of these new features will be saveable alongside the existing (and improved) features of VMM files.

## Why 4DMM?

Modern 3D applications are extraordinarily capable, but that capability often comes with a large interaction and workflow burden that can take years to master even the basics. Original 3D Movie Maker approached the problem from the opposite direction: learn while doing--grab something, move it, animate it, add sound, and make a movie. Expanding on this approach is the foundation of 4DMM.

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

# KBD Control:

## GENERAL EDITOR CONTROLS:

### Frame buttons:
<kbd>CTRL</kbd>+NEXT FRAME - Duplicates the current frame after it and selects the new frame.
<kbd>CTRL</kbd>+PREVIOUS FRAME - Duplicates the current frame before it and selects the new frame.
<kbd>CTRL</kbd>+<kbd>SHIFT</kbd>+NEXT FRAME/PREVIOUS FRAME - Uses the same placement behavior, but the inserted frame contains no actors, props, or text boxes for that frame only.
<kbd>CTRL</kbd>+<kbd>ALT</kbd>+NEXT FRAME/PREVIOUS FRAME - Opens a dialog asking how many frames to insert from the current frame.
<kbd>CTRL</kbd>+<kbd>ALT</kbd>+<kbd>D</kbd> - Deletes frames in bulk from the current position. Similar to <kbd>CTRL</kbd>+<kbd>ALT</kbd>+NEXT FRAME or <kbd>CTRL</kbd>+<kbd>ALT</kbd>+PREVIOUS FRAME, except it deletes the number of frames you enter starting from the currently displayed frame.

## ACTORS + PROPS TAB CONTROLS

<kbd>F</kbd> - Enters Free Camera Mode.
<kbd>ALT</kbd>+<kbd>Y</kbd> - Toggles the selection box color between the default yellow and the new shaded grey, which shows how light is hitting the selected object.
<kbd>CTRL</kbd>+GROW/SHRINK TOOL - Allows an object to be grown beyond 10x size, up to 100x size.
<kbd>CTRL</kbd>+<kbd>SHIFT</kbd>+<kbd>L</kbd> - Toggles between lights on and the default 3DMM lights.
<kbd>CTRL</kbd>+<kbd>ALT</kbd>+<kbd>L</kbd> - Toggles the visibility of invisible object lights.
<kbd>CTRL</kbd>+<kbd>~</kbd> - Opens settings.
<kbd>ESC</kbd> - Unselects the currently selected object and removes the selection box.

### MULTI SELECT + SELECTION TOOLS

<kbd>SHIFT</kbd>+LEFT CLICK - Allows selection of more than one object in the viewport. This also works in the prop browser.
<kbd>SHIFT</kbd>+RIGHT CLICK - Deselects an object in the viewport. This also works in the prop browser.
<kbd>CTRL</kbd>+<kbd>S</kbd> - Makes an object non-selectable. Once non-selectable, the object appears with an "(X)" in front of it in the objects browser. Double-clicking the object in the browser makes it selectable again. Alternatively, double-click the object in the objects browser to toggle its selectability.
<kbd>ALT</kbd>+CLICK AND DRAG - Allows tools to be applied to objects that are otherwise difficult or impossible to click because of their current size or position relative to other objects. Once the object is selected, hold the <kbd>ALT</kbd> key and click or click and drag anywhere on the screen to use the current tool on the selected object.

### ACTION TOOL:

RIGHT CLICK - Right-clicking an object (Actor, Prop, or 3D Word) with the ACTION TOOL selected opens Actor Studio and automatically selects the object you right-clicked.

### 3D Words Window controls:

<kbd>ALT</kbd>+<kbd>C</kbd> - Opens the color picker.
RIGHT CLICK on TEXTURE SELECT BUTTON - Opens the Texture Browser.

### Costume Dresser Window controls:

<kbd>ALT</kbd>+<kbd>C</kbd> - Opens the color picker for basic shape props. Try <kbd>CTRL</kbd>+<kbd>C</kbd> if <kbd>ALT</kbd>+<kbd>C</kbd> is not working.

## ACTOR STUDIO

<kbd>CTRL</kbd>+<kbd>UP ARROW</kbd>/<kbd>CTRL</kbd>+<kbd>DOWN ARROW</kbd> - Selects the next or previous frame of the currently selected animation (Action).
<kbd>ALT</kbd>+<kbd>UP ARROW</kbd>/<kbd>ALT</kbd>+<kbd>DOWN ARROW</kbd> - Selects the next or previous item in the body parts list.
<kbd>UP ARROW</kbd>/<kbd>DOWN ARROW</kbd> - Moves the current selection up or down in the last list (Frames/Parts) that was clicked.
<kbd>CTRL</kbd>+<kbd>1</kbd> - Selects the POSITION TOOL.
<kbd>CTRL</kbd>+<kbd>2</kbd> - Toggles the UP/DOWN checkbox for the POSITION TOOL and selects the POSITION TOOL.
<kbd>CTRL</kbd>+<kbd>Q</kbd> - Selects the PITCH ROTATION TOOL.
<kbd>CTRL</kbd>+<kbd>W</kbd> - Selects the YAW ROTATION TOOL.
<kbd>CTRL</kbd>+<kbd>E</kbd> - Selects the ROLL ROTATION TOOL.
<kbd>CTRL</kbd>+<kbd>R</kbd> - Selects the RESET ROTATION TOOL.
<kbd>CTRL</kbd>+<kbd>A</kbd> - Selects the GROW/SHRINK TOOL.
<kbd>CTRL</kbd>+<kbd>S</kbd> - Selects the STRETCH/SQUASH TOOL.
<kbd>CTRL</kbd>+<kbd>D</kbd> - Selects the GROW/SHRINK/STRETCH/SQUASH RESET TOOL.

### Actor Studio Camera Controls (for use in the main Actor Studio window, called the Actor Studio Viewport [ASV])

<kbd>CTRL</kbd>+MOUSE1 - Hold down in the ASV to move the camera around the object or object group(s).
MOUSEWHEEL UP/DOWN - Zooms in and out.
MOUSE3 - Hold down to adjust the height of the camera.
<kbd>ALT</kbd>+MOUSE1 - Hold down in the ASV to use the currently selected tool on the contents of the selection box. This is intended for cases where a selection will not stay selected when clicked.
<kbd>CTRL</kbd>+<kbd>C</kbd> - Copies the contents of the selection box.
<kbd>CTRL</kbd>+<kbd>V</kbd> - Pastes the contents of the selection box into the currently selected grouping in the parts list, or the nearest hierarchically valid grouping.

## Frame by Frame Moving Camera Controls

### MANUAL CAMERA MODE

MANUAL CAMERA MODE - Enters Manual Camera Mode. The MANUAL CAMERA MODE button is the second red-light toggle to the left of the SCISSORS buttons in the middle of the SCENE tab.
<kbd>LEFT ARROW</kbd>/<kbd>RIGHT ARROW</kbd> - Saves, then moves to the adjacent frame unless the frame has a camera motion tween beginning in it.
<kbd>E</kbd> - Saves the live camera and opens the Manual Camera properties window.
<kbd>CTRL</kbd>+MANUAL CAMERA MODE - When the red light is lit, opens the Manual Camera properties window.
MANUAL CAMERA MODE red light - Indicates that a Manual Camera position has been defined for the current frame.

### Depth Camera Tween

FIRST TOGGLE TO THE LEFT OF THE SCISSORS BUTTONS - Opens the Depth Tween properties window. This allows editing the tween distance between two frames, the frame range for the tween, and the camera angle used for the entire tween.
SEMI-INVISIBLE BUTTON - Aligns the Depth Tween's yaw angle with the longest edge of an object whose long edge extends away from the camera, such as when the camera is looking down a street. To use it, first select the object with the POSITION (HAND) TOOL in the ACTORS AND PROPS tab so the object's yellow selection box appears. Then click the SCENE tab and click the SEMI-INVISIBLE BUTTON. It is the first dark-grey square ("unused" button slot) to the left of the SCISSORS buttons. Work in progress: an icon exists for this button, but it has not been implemented yet because there may still be significant UI changes and the button's location may change.
RED-LIGHT TOGGLE on the far right of the ACTORS AND PROPS tab - Allows an actor's movement to be synchronized with the camera's motion tween, or not. For example, if you click and hold the mouse button to make an actor run, the actor will run at the same speed as the camera moves automatically. This eliminates the need for manual frame-by-frame clicking and dragging to make an actor move at the same pace as the camera.

## Free Camera Mode

Free Camera Mode is for editing a custom scene from different camera angles and is only available while the ACTORS AND PROPS tab is open.
<kbd>F</kbd> - Enables or disables Free Camera Mode. Pressing <kbd>F</kbd> while Free Camera Mode is active freezes the camera where it currently is and exits Free Camera Mode. This allows props to be edited from a different camera position.
LEFT CLICK or <kbd>TAB</kbd> - Freezes the camera where it currently is and exits Free Camera Mode.
<kbd>ESC</kbd> - Exits Free Camera Mode and resets the camera to its current position for the current frame.
<kbd>W</kbd>/<kbd>A</kbd>/<kbd>S</kbd>/<kbd>D</kbd> + MOUSE X/MOUSE Y - Provides standard first-person-game-style camera movement.
<kbd>E</kbd>/<kbd>Q</kbd> - Adjusts the roll angle. It works, but there is room for improvement.
<kbd>SHIFT</kbd> - Enables or disables the ability to change the camera height. Disabling this after flying upward results in the Free Camera being unable to change its Y value at the current altitude.
CONSOLE (<kbd>CTRL</kbd>+<kbd>~</kbd>) → ADVANCED SETTINGS - Opens the Free Camera settings.

# HOW TO START 4DMM AFTER YOU HAVE BUILT (COMPILED) THE SOFTWARE

## Here's instructions on how to start 4DMM so that all features are enabled.

- There's no way to enable, disable or modify some settings after start up. It might take multiple start up attempts to get 4DMM's external windows to look correct depending on your resolution and Windows settings.
First, create a shortcut to 3dmovie.exe after you have built (compiled) it.
### IMPORTANT - YOU MUST ADJUST DPI SCALING TO BE COMPLETED BY APPLICATION:
- ALT+DOUBLE CLICK on the shortcut to 3dmovie.exe (or right click on 3dmovie.exe and then left click on properties in the menu that appears) 
- Click on the "Compatibility" tab
- Click on "Change high DPI settings." If multiple Windows user accounts will be using this shortcut to start 4DMM, click on "Change settings for all users" then "Change high DPI settings."
- Check the box next to "Override high DPI scaling behavior."
- Click the dropdown menu below the checkbox and then click on "Application" in the menu that appears.
- Click the OK button to close each window, making sure to click the "Apply" button before clicking the OK button if the window has an "Apply" button.
(Please note that the above DPI setting will apply to all shortcuts you make to 3dmovie.exe so it shouldn't be necessary to do this again).

## Use the following arguments for starting 4DMM on a 4K (2160p) monitor:
3dmovie.exe -w -e -c -a -l -u -sh -multi -resolution 4.25x -cursor_size 2x -gui_scale 2

Please note that it might be necessary to change the "-gui_scale 1" argument to "-gui_scale 2" for certain external windows to be fully usable.\*\*\*
**Use the following arguments for starting 4DMM on a 2560x1440 (1440p) resolution monitor:**
3dmovie.exe -w -e -c -a -l -u -sh -multi -resolution 1320 -cursor_size 1.5x -gui_scale 1
**Use the following arguments for starting 4DMM on a 1920x1080 (1080p) resolution monitor:**
3dmovie.exe -w -e -c -a -l -u -sh -multi -resolution 960 -cursor_size 1x -gui_scale 1

\*\*\*  If an external window appears with missing/cut-off text or missing/cut-off buttons, please try adjusting the gui_scale argument. This setting might require using different values due to your Windows DPI scaling settings. Generally the first step is to try both the "-gui_scale 1" argument and the "-gui_scale 2" argument. Other forms of this argument are also valid, for example:
-gui_scale 1.5
-gui_scale 1.2
-gui_scale 1.1
-gui_scale 0.5
-gui_scale 2.5
etc.

# ARGUMENTS

##DETAILED ARGUMENT INFORMATION

-c - color depth argument runs 4DMM in 24-bit RGB888 color mode
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
