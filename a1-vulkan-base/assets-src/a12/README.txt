## Sun Temple Model

WARNING: MODIFIED FOR COMP5892M! DO NOT DISTRIBUTE OUTSIDE COMP5892M!
WARNING: MODIFIED FOR COMP5892M! DO NOT DISTRIBUTE OUTSIDE COMP5892M!
WARNING: MODIFIED FOR COMP5892M! DO NOT DISTRIBUTE OUTSIDE COMP5892M!

The UE4 Sun Temple Model is sourced from NVIDIA ORCA:

https://developer.nvidia.com/orca
https://developer.nvidia.com/ue4-sun-temple

(It has been donated to NVIDIA ORCA by Epic Games. See page above for details.)

License: CC-BY-NC-SA (Creative Commons Non-Commercial Share-Alike)

The model contains about 1.6M vertices.


The original model comes in the FBX format with DDS textures. For CW3, it's
been converted to Wavefront OBJ, while carefully preserving/recovering
material data from the FBX file. 

Textures were converted to PNG, splitting roughness+metalness into separate
files, and scaling the textures by 50% (from 2048x2048 to 1024x1024). Size on
disk shrinks from ~450MB to ~130MB. VRAM requirements are also significantly
reduced.

As a workaround to some limitations (file size on Github), the .OBJ file is
compressed with ZStandard compression (https://github.com/facebook/zstd); this
brings it down from the original ~180MB to about 20MB. (And to further over-
engineer things, the Wavefront OBJ loader has been updated to read the
compressed file directly.)
