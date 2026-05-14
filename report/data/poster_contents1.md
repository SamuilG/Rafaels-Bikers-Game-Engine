# Steer Engine Poster Contents

## Overview

The Steer Engine brings high-performance rendering and complex physics simulation to the forefront of game development.

Drawing inspiration from third-person action and racing titles, the engine delivers seamless vehicle and character gameplay with fluid, responsive controls.

Steer Engine allows rapid development by leveraging a robust Entity Component System and a fully integrated physics wrapper that allows the developer to have complete control of the gameplay.

Steer Engine provides multiple features for a game developer to use, but the main focal points for the engine are its renderer, architecture systems, and physics.

> **[FIGURE 1: A view of the main menu or starting scene, showing the engine's clean UI and theme.]**

## Architecture & Systems

**Flecs - Entity Component System:** The engine uses Flecs as a cache-friendly Entity Component System; it represents an entity which can have any combination of different components. The engine supports grouping and transitioning these entities dynamically. Basic entities act purely as an interface, enabling highly decoupled game logic.

**Scene Management & LOD:** This system allows the developer to spawn complex pre-fab objects and automatically apply distance-based Level of Detail (LOD) and frustum culling. This system ensures optimal performance in expansive environments.

> **[FIGURE 2: A bird's eye view of the entire level, showing the scale and layout of the game environment.]**

**Asset Loader:** Steer Engine features a robust GLTF model and scene loader. Developers can easily import meshes, PBR materials, skeletons, and animations from standard GLTF files dynamically.

**Event Handling & Input:** The developer can bind runtime action-based events utilizing a queueing system. The input system natively supports controller input and Rumble Feedback alongside keyboard and mouse, making gameplay hardware-agnostic.

**Audio:** The engine integrates the Miniaudio library to give game developers the ability to play audio on command, allowing for background tracks as well as spatial 3D audio. This is directly integrated with the ECS system.

## Rendering

**Default Features:** Physically Based Rendering (PBR), Cascaded Shadow Maps, Frustum Culling, Directional/Point/Spot Lighting, Light Attenuation, and Forward Rendering.

**Artistic Stylisation / Post Processing Effects:** Multipass Bloom, Screen Space Ambient Occlusion (SSAO), Screen Space Reflections (SSR), Distance-based LOD, Skeletal Mesh Skinning, Emissive Layers, and Transparent Rendering.

The out-of-the-box features contain a custom Vulkan rendering pipeline with extensive utilities to set up the scene of your choice. The post-processing effects give the user a detailed and configurable setup to enhance realism. For example, SSAO creates accurate contact shadows, and SSR enables dynamic reflective surfaces. We also provide native support for complex humanoid skeletal animations using compute skinning, allowing complete custom control over rendering styles.

> **[FIGURE 3: A close-up of the bike where you can clearly see the realistic reflections and shadows on the surface.]**

## User Interfaces

**Debugging:** Leveraging Dear ImGui for real-time visibility into ECS states, rendering batches, and specialized **Runtime UI Debugging** (bounding boxes, hit-rects, and live data-binding inspection).

**Custom UI System:** A bespoke framework and visual editor for managing serialized elements and hierarchical layouts. It features a decoupled, high-performance runtime with built-in picking and inspection for polished interfaces.




> **[FIGURE 4: The custom Visual UI Editor in action, showing the hierarchical layout and real-time element manipulation.]**

## Physics & Dynamic Feedback

**Jolt Physics Engine:** Steer Engine utilizes the Jolt Physics library for its physics system, utilizing their multithreaded system for collision handling, rigidbody calculations, and layering.

**Entities & Rigidbody Types:** The game developer is offered multiple different physics body types. It natively supports static, dynamic, custom kinematic, and compound physics bodies smoothly integrated with the ECS.

**Character & Vehicle Mechanics:** The engine provides support for complex constraints and rider bindings, allowing humanoid entities to seamlessly mount and control vehicles like bikes within the engine.

**Event-Driven Particles:** A high-performance, vertex-buffer based system supporting configurable emitter shapes (Cone, Sphere, Box). The system is fully integrated with the ECS and event handling to provide real-time visual feedback for physics-driven gameplay events.

> **[FIGURE 5: A cool action shot of the bike jumping mid-air, showing the physics and animation working together.]**

> **[FIGURE 6: A screenshot of the bike braking hard, with the particle system producing a cloud of dust from the rear tire.]**



## Results

Steer Engine provides a highly performative engine while implementing advanced rendering passes and post-processing effects, allowing for stunning visual fidelity.

Rendering a complex GLTF scene with materials and animations is streamlined and requires minimal code.

Using the engine's default Jolt physics integration and ECS achieves a robust and reliable game loop.

Complex game mechanics are easy to implement with the action-based input system and entity grouping, allowing the developer a direct interface into gameplay and rendering.

The debugging features allow the user to instantly view ECS states and colliders, drastically reducing iteration time. Steer Engine provides all of this and more while being highly performant utilizing modern Vulkan API features.

## Team Information

University of Leeds.

Members: [Add your team members' names here]
