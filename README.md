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
<kbd>CTRL</kbd>+NEXT FRAME - Duplicate after current and select the new frame.  
<kbd>CTRL</kbd>+PREVIOUS FRAME - Duplicate before current and select the new frame.  
<kbd>CTRL</kbd>+<kbd>SHIFT</kbd>+NEXT FRAME/PREVIOUS FRAME - Same placement, but the inserted frame contains no actors, props, or text boxes for that frame only.  
<kbd>CTRL</kbd>+<kbd>ALT</kbd>+NEXT/PREVIOUS - Pops up dialog asking how many frames to insert from current frame.  
<kbd>CTRL</kbd>+<kbd>ALT</kbd>+<kbd>D</kbd> - Delete frames in bulk from current position. Similar to <kbd>CTRL</kbd>+<kbd>ALT</kbd>+NEXT FRAME or <kbd>CTRL</kbd>+<kbd>ALT</kbd>+PREVIOUS FRAME except deletes the number of frames you enter from the currently displayed frame.  

## ACTORS + PROPS TAB CONTROLS
<kbd>F</kbd> - Enter free camera mode.  
<kbd>ALT</kbd>+<kbd>Y</kbd> - Toggles selection box color between the default yellow and the new shaded grey which shows how light is hitting the selected object.  
<kbd>CTRL</kbd>+Grow/Shrink tool - Allows for growing object beyond 10x size to 100x size.  
<kbd>CTRL</kbd>+<kbd>SHIFT</kbd>+<kbd>L</kbd> - Toggles between lights on and default 3dmm lights.  
<kbd>CTRL</kbd>+<kbd>ALT</kbd>+<kbd>L</kbd> - Toggles invisible object lights visibility.  
<kbd>CTRL</kbd> + <kbd>~</kbd> and <kbd>CTRL</kbd>+<kbd>~</kbd> - Opens settings.  
<kbd>ESC</kbd> - Unselect the currently selected object (removes the selection box).  

### MULTI SELECT + SELECTION TOOLS

<kbd>SHIFT</kbd> + LEFT CLICK - Allows for selection of more than 1 object in the viewport (also works in prop browser -- add).  
<kbd>SHIFT</kbd> + RIGHT CLICK - De-selects an object in the viewport (also works in prop browser -- add).  
<kbd>CTRL</kbd>+<kbd>S</kbd> - Makes an object no longer selectable. Once non-selectable, the object will appear with an "(X)" in front of it in the objects browser. Double clicking on it in the browser makes the object selectable again. Alternatively, simply double clicke on the object in the objects browser to toggle the selectability.  
<kbd>ALT</kbd>+CLICK AND DRAG - This allows for applying tools to objects which are otherwise not selectable due to their current size or position in relation to other objects. Once the object is currently selected, simply hold the <kbd>ALT</kbd> key and click or click and drag any where on the screen to use the current tool on the object.  

### ACTION TOOL:
RIGHT-CLICK.  
Right-clicking on an object (Actor, Prop or 3D Word) with the Action tool opens Actor Studio and triggers Actor Studio to automatically select the object you right clicked on upon opening.  

### 3D Words Window controls:

<kbd>ALT</kbd>+<kbd>C</kbd> - Opens up color picker.  
RIGHT CLICK on TEXTURE SELECT BUTTON - Opens up the Texture Browser.  

### Costume Dresser Window controls:

<kbd>ALT</kbd>+<kbd>C</kbd> - Opens up color picker for basic shape props (try <kbd>CTRL</kbd>+<kbd>C</kbd> if <kbd>ALT</kbd>+<kbd>C</kbd> is not working).  

## ACTOR STUDIO

<kbd>CTRL</kbd>+<kbd>UP ARROW</kbd> / <kbd>CTRL</kbd>+<kbd>DOWN ARROW</kbd> - Selects the next/previous frame of the currently selected animation (action).  
<kbd>ALT</kbd>+<kbd>UP ARROW</kbd>/ <kbd>ALT</kbd>+<kbd>DOWN ARROW</kbd> - Selects the next/previous item in the body parts list.  
<kbd>UP ARROW</kbd>/<kbd>DOWN ARROW</kbd> - Moves the current selection up/down in the last list (Frames/Parts) which was clicked on.  
<kbd>CTRL</kbd>+<kbd>1</kbd> - Selects the Position tool.  
<kbd>CTRL</kbd>+<kbd>2</kbd> - Toggles the Up/Down checkbox for the Position tool and selects the Position tool.  
<kbd>CTRL</kbd>+<kbd>Q</kbd> - Selects the Pitch rotation tool.  
<kbd>CTRL</kbd>+<kbd>W</kbd> - Selects the Yaw rotation tool.  
<kbd>CTRL</kbd>+<kbd>E</kbd> - Selects the Roll rotation tool.  
<kbd>CTRL</kbd>+<kbd>R</kbd> - Selects the Reset rotation tool.  
<kbd>CTRL</kbd>+<kbd>A</kbd> - Selects the Grow/Shrink tool.  
<kbd>CTRL</kbd>+<kbd>S</kbd> - Selects the Stretch/Squash tool.  
<kbd>CTRL</kbd>+<kbd>D</kbd> - Selects the Grow/Shrink/Stretch/Squash Reset tool.  

### Actor Studio Camera Controls (For use in main Actor Studio window which is called the Actor Studio Viewport [ASV])

<kbd>CTRL</kbd>+MOUSE1 - Hold down in ASV to move the camera around the object or object group(s).  
MOUSEWHEEL UP/DOWN - Zooms in and out.  
MOUSE3 - Hold down to adjust the height of the camera.  
<kbd>ALT</kbd>+MOUSE1 - Hold down in ASV to use the currently selected tool on the contents of the selection box (intended to be used when a selection won't stay selected when clicking on it).  
<kbd>CTRL</kbd>+<kbd>C</kbd> - Copies the contents of the selection box.  
<kbd>CTRL</kbd>+<kbd>V</kbd> - Pastes the contents of the selection box into the currently selected grouping in the parts list, or the nearest (hierarchically speaking) valid grouping.  

## Frame by Frame Moving Camera Controls

**These tools allows for automatically moving the camera across multiple frames so that it can do things like follow an actor, explore a scene, follow a carchase, . Yes, the camera is actually moving. No, the props are not moving. The camera is moving**

### MANUAL CAMERA MODE

**Click on Toggle** - Enters manual camera mode [Manual Camera Mode is the 2nd red light toggle to the left of the Scissors buttons in the middle of the SCENE tab]. Clicking on it will immediately make the cursor disappear and allow you to move the camera with the mouse and the W, A, S, D keys similar to a first person game. The camera can also be rolled using the E and Q keys although this feature is still a work in progress.
<kbd>LEFT</kbd>/<kbd>RIGHT</kbd> - Saves, then moves to the previous/next frame (respectively).  
<kbd>E</kbd> - Saves the live camera and opens the manual camera properties window.  
<kbd>CTRL</kbd>+Click Manual Camer Mode button when red light is lit - Opens the manual camera properties window.  
*When the red light is on underneath the Manual Camera mode button, this indicates that a Manucal Camera position has been defined for the current frame.*  

### Depth Camera Tween (Drone / Dolly Camera Movement) 

**You can think of this tool as being sort of like having a camera mounted on a quadcopter flying through your scene. Or another way to think of it would be like putting the camera on rails or on a dolly**
- Click on the 1st toggle from the middle under scenes: Enters depth tween properties window, allows for editing distance of tween between two frames, as well as the frame range for the tween, and the camera angle that will be used for the entire tween.  
- Semi-Invisible button: Aligns depth tween's yaw angle with the longest edge of object that has its long edge going away from the camera, like as if the camera was looking down a street. To use it, first select the object using the Position (Hand) tool in the ACTORS AND PROPS tab so the object's yellow selection box appears. Then click the SCENE tab and click the Semi-Invisible button: it's the 1st dark grey square ("unused" button slot) to the left of the Scissor-icon buttons. 
- The red light toggle which is the farthest to the right of all of the red light toggles in the ACTORS AND PROPS tab: Allows for synchronizing an actors movement with the camera's motion tween, or not. Adjusts the way that an animation can be clicked and dragged This means if you're clicking and holding down the mouse button to make an actor run, for instance, the actor will run at the same speed in which the camera moves automatically. Eliminates the need for manual frame-by-frame clicking and dragging to have an actor move at the same pace as the camera).  
**(Work in progress: I have an icon completed for the invisible button as well as for one of the toggles but these haven't been implemented yet as there might be significant UI changes so I'm going to wait until I have a finalized plan for how the new main app window UI changes will be laid out before giving the buttons their final apperance)).**

## Free Camera Mode

For editing a custom scene from different camera angles - only available with the ACTORS and PROPS tab open.  
<kbd>F</kbd> - Enables/disables free cam mode. Pressing <kbd>F</kbd> while free cam mode is active freezes the cam where it currently is and exits free cam mode. This allows for editing props from a different camera position.  
LEFT CLICK or <kbd>TAB</kbd> - Pressing left click while free cam mode is active freezes the cam where it currently is and exits free cam mode.  
<kbd>ESC</kbd> - Exits free cam mode, resetting the camera to its current position for the current frame.  
<kbd>W</kbd>,<kbd>A</kbd>,<kbd>S</kbd>,<kbd>D</kbd>, mouse x, and mouse y - Standard first person game style camera movement.  
<kbd>E</kbd> and <kbd>Q</kbd> - Adjust roll angle (it works but there's room for improvement).  
<kbd>SHIFT</kbd> - Enables/disables the ability to change the height of the camera. Disabling this after 'flying upwards' results in the free camera then being incapable of changing its y-value at the current altitude.  
Access the free cam settings in the CONSOLE (<kbd>CTRL</kbd> + <kbd>~</kbd>) by clicking the ADVANCED SETTINGS button.  

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

## DETAILED ARGUMENT INFORMATION

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
**`-resolution 1080`**  - This argument can use resolution height numbers. Width is calculated automatically. If you use resolution height numbers and are running the app in windowed mode (recommended), subtract 120 from your current resolution's height. So 1920x1080 or 1080p would have a height of 1080 pixels, so that would be `-resolution 960`, 1280x720 AKA 720p would be `-resolution 600`, 2560x1440 AKA 1440p would be `-resolution 1320`, and 4K AKA 2160p would be `-resolution 2040`.
-resolution 4.24x - This argument can **also** use multiples of 480 to calculate the height. Multiples are numbers ending in `x`. For example: `-resolution 1.1x`, `-resolution 1.25x`, `-resolution 2x`, `-resolution 2.5x`, `-resolution 4.25x`, etc. This option gives you another way to resize the app if you want to run it at a resolution which doesn't take up your entire screen in windowed mode (although you can of course specify your full-size app resolutions with this as well). If you're familiar with how big a 640x480 window is on your monitor, then you can decide if you want to have the app run at 1.5 times that size, or 2.25 times that size, etc.
-cursor_size 2x scales the cursor size up by a factor of 2. So this argument allows for adjusting the size of the cursor using scaling similar to the resolution argument

## Compatibility

Preserving the original 3DMM experience and its existing movie/asset ecosystem is an important design goal. New functionality should extend the program without unnecessarily destroying compatibility with existing 3DMM content or the directness of the original workflow.

## Current state

A large amount of 4DMM is already functional, but the project is moving quickly. Expect features in varying degrees of completeness, debugging instrumentation, changing UI, experimental renderer work, and the occasional piece of 1995 code discovering a new and creative way to object to the year 2026.

## License and trademarks

4DMM inherits code from upstream open-source projects. **Keep the repository's existing `LICENSE`, `THIRD_PARTY_LICENSES.txt`, attribution, and other upstream legal files intact.** Individual bundled dependencies may have their own license terms.

Microsoft, 3D Movie Maker, and related names/logos are trademarks of their respective owners. 4DMM is an independent community project and is not an official Microsoft product or endorsed by Microsoft.
