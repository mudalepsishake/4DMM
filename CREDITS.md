# Credits and project lineage

4DMM is a continuation and major expansion of Microsoft 3D Movie Maker built from the 3DMMEx codebase.

## Project lineage

- **Microsoft 3D Movie Maker** — original application and source code, released by Microsoft under the MIT License. [https://github.com/microsoft/Microsoft-3D-Movie-Maker](https://github.com/microsoft/Microsoft-3D-Movie-Maker)
- **3DMMForever** — Foone Turing's modernization work following Microsoft's source release.
- **3DMMEx** — Ben Stone's source port and modernization work, which is the direct upstream base of 4DMM. [https://github.com/benstone/3dmmex](https://github.com/benstone/3dmmex)
- **BRender** — the original rendering engine developed by Argonaut Software / Argonaut Technologies.
- **[Blazing Renderer (BRender) 1.4](https://github.com/BlazingRenderer/BRender)** — the modernized BRender fork used by 4DMM's modern OpenGL renderer, maintained by the BlazingRenderer project and based on the open-source BRender releases. Here is a working mirror (as of 9-26-2026) of the BRender project's website: [https://bayareaengineers.com/blazingrender.net](https://www.bayareaengineers.com/blazingrender/)

## Historical tools and compatibility references

- **Croc: Legend of the Gobbos (2025 Edition)** - The modern remaster was used as an important technical reference during 4DMM’s transition to modern BRender 1.4, particularly for studying the relationship between legacy BRender assets and a modern rendering pipeline. No Croc game assets or source code are distributed with 4DMM.
- **Holmstrom's 3DMM camera-control utility** — an external 3DMM camera-navigation tool distributed as `MPR.dll`. The DLL was reverse-engineered as a technical reference during the initial 4DMM camera-track work. No Holmstrom source code was available to the 4DMM project, and 4DMM does not bundle `MPR.dll`.
- **7gen / OBJ2VXP** by Foone Turing — part of the historical 3DMM expansion-tool ecosystem and useful as a compatibility/reference point for 3DMM expansion formats. 7gen is not bundled with 4DMM.