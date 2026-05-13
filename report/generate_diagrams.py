#!/usr/bin/env python3
"""
Generate four academic architecture diagrams for the Steer Engine report.
All structures derived from actual C++ codebase analysis.
Uses matplotlib (no graphviz dot binary required).
"""
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
import os

OUT_DIR = os.path.join(os.path.dirname(__file__), 'figures')
os.makedirs(OUT_DIR, exist_ok=True)

# ── Shared styling ──────────────────────────────────────────────
FONT       = {'family': 'DejaVu Sans', 'size': 9}
TITLE_SIZE = 12
BG_COLOR   = '#FAFAFA'
# Palette
C_BLUE   = '#4A90D9'
C_GREEN  = '#5CB85C'
C_ORANGE = '#F0AD4E'
C_RED    = '#D9534F'
C_PURPLE = '#9B59B6'
C_TEAL   = '#1ABC9C'
C_GRAY   = '#95A5A6'
C_DARK   = '#2C3E50'

def draw_box(ax, x, y, w, h, text, color=C_BLUE, fontsize=8, textcolor='white', alpha=0.92):
    """Draw a rounded rectangle with centered text."""
    box = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.08",
                         facecolor=color, edgecolor='#333333', linewidth=1.0, alpha=alpha)
    ax.add_patch(box)
    ax.text(x + w/2, y + h/2, text, ha='center', va='center',
            fontsize=fontsize, color=textcolor, fontweight='bold', wrap=True)
    return box

def draw_arrow(ax, x1, y1, x2, y2, color='#333333', style='->', lw=1.2):
    """Draw an arrow between two points."""
    ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle=style, color=color, lw=lw))

def draw_label(ax, x, y, text, fontsize=7, color='#555555', ha='center'):
    ax.text(x, y, text, ha=ha, va='center', fontsize=fontsize, color=color, style='italic')


# ════════════════════════════════════════════════════════════════
# DIAGRAM 1: Flecs Component-Space Architecture
# Source: SceneManager.hpp (components), SceneManager.cpp (systems)
# ════════════════════════════════════════════════════════════════
def generate_flecs_diagram():
    fig, ax = plt.subplots(1, 1, figsize=(10, 7))
    ax.set_xlim(0, 10); ax.set_ylim(0, 7)
    ax.set_aspect('equal'); ax.axis('off')
    fig.patch.set_facecolor(BG_COLOR)
    ax.set_title('Flecs Component-Space Architecture', fontsize=TITLE_SIZE, fontweight='bold', pad=15)

    # ── Layer: Entity Types (top) ──
    draw_label(ax, 5, 6.7, 'Entity Layer', fontsize=9, color=C_DARK)
    draw_box(ax, 0.3, 6.0, 1.8, 0.5, 'Static Meshes', C_BLUE)
    draw_box(ax, 2.4, 6.0, 1.8, 0.5, 'Dynamic Bodies', C_GREEN)
    draw_box(ax, 4.5, 6.0, 1.8, 0.5, 'Compound Phys.', C_ORANGE)
    draw_box(ax, 6.6, 6.0, 1.8, 0.5, 'Animated (Skin)', C_PURPLE)
    draw_box(ax, 8.5, 6.0, 1.2, 0.5, 'Lights', C_RED)

    # ── Layer: Components (middle) ──
    draw_label(ax, 5, 5.2, 'Component Layer  (Archetypes / Tables)', fontsize=9, color=C_DARK)
    comps = [
        (0.2, 4.3, 'LocalTransform\nglm::mat4', C_TEAL),
        (2.0, 4.3, 'WorldTransform\nglm::mat4', C_TEAL),
        (3.8, 4.3, 'MeshComponent\nuint32 meshIdx', C_BLUE),
        (5.6, 4.3, 'MaterialComp.\nuint32 matIdx', C_BLUE),
        (7.4, 4.3, 'PhysicsBody\nuint32 bodyID', C_GREEN),
    ]
    for cx, cy, txt, col in comps:
        draw_box(ax, cx, cy, 1.6, 0.7, txt, col, fontsize=7)

    # Extra components row
    comps2 = [
        (0.2, 3.3, 'LODComponent\n3 levels + bias', C_GRAY),
        (2.0, 3.3, 'AnimationComp.\nclip + time', C_PURPLE),
        (3.8, 3.3, 'SkinComponent\nboneMatrices[]', C_PURPLE),
        (5.6, 3.3, 'OpacityComp.\nalpha blending', C_ORANGE),
        (7.4, 3.3, 'EntityStatus\nrender + physics', C_GRAY),
    ]
    for cx, cy, txt, col in comps2:
        draw_box(ax, cx, cy, 1.6, 0.7, txt, col, fontsize=7)

    # ── Arrows from entities to components ──
    for ex in [1.2, 3.3, 5.4, 7.5]:
        draw_arrow(ax, ex, 6.0, ex, 5.05, lw=0.8)
    draw_arrow(ax, 9.1, 6.0, 8.2, 5.05, lw=0.8)

    # ── Layer: Systems (bottom) ──
    draw_label(ax, 5, 2.6, 'System Layer  (Flecs Queries → per-frame iteration)', fontsize=9, color=C_DARK)
    systems = [
        (0.3, 1.5, 2.0, 'UpdateWorldTransform\nLocal × Parent → World', C_DARK),
        (2.6, 1.5, 2.0, 'get_render_batches()\nFrustum Cull + LOD', C_DARK),
        (5.0, 1.5, 2.2, 'PhysicsSync\nJolt ↔ WorldTransform', C_DARK),
        (7.5, 1.5, 2.2, 'get_skinned_batches()\nBone SSBO upload', C_DARK),
    ]
    for sx, sy, sw, txt, col in systems:
        draw_box(ax, sx, sy, sw, 0.8, txt, col, fontsize=7)

    # Arrows from components to systems
    for sx in [1.0, 3.0, 5.0, 8.0]:
        draw_arrow(ax, sx, 3.3, sx, 2.35, lw=0.8, color=C_GRAY)

    # ── Output row ──
    draw_box(ax, 2.5, 0.3, 2.3, 0.6, 'RenderBatch[]\n→ Vulkan Draw', C_RED, fontsize=8)
    draw_box(ax, 5.5, 0.3, 2.5, 0.6, 'Bone Matrices[]\n→ GPU SSBO', C_PURPLE, fontsize=8)
    draw_arrow(ax, 3.6, 1.5, 3.65, 0.95, lw=1.0, color=C_RED)
    draw_arrow(ax, 8.6, 1.5, 6.75, 0.95, lw=1.0, color=C_PURPLE)

    plt.tight_layout()
    path = os.path.join(OUT_DIR, 'flecs_component_space.png')
    fig.savefig(path, dpi=200, bbox_inches='tight', facecolor=BG_COLOR)
    plt.close(fig)
    print(f'  ✓ {path}')


# ════════════════════════════════════════════════════════════════
# DIAGRAM 2: Vulkan Render Pass Flowchart
# Source: rendering.cpp record_commands(): exact pass order
# ════════════════════════════════════════════════════════════════
def generate_render_pass_diagram():
    fig, ax = plt.subplots(1, 1, figsize=(11, 8))
    ax.set_xlim(0, 11); ax.set_ylim(0, 8)
    ax.set_aspect('equal'); ax.axis('off')
    fig.patch.set_facecolor(BG_COLOR)
    ax.set_title('Vulkan Dynamic Rendering: Multi-Pass Flowchart', fontsize=TITLE_SIZE, fontweight='bold', pad=15)

    bw, bh = 2.2, 0.65  # box width/height
    cx = 4.4  # center x for main column

    # Pass 0: Shadow
    draw_box(ax, cx, 7.0, bw, bh, 'Shadow Pass\n(N cascades × depth)', C_DARK, fontsize=8)
    draw_label(ax, cx+bw+0.3, 7.3, 'Depth-only\nCSM layers', fontsize=6.5)

    # Pass 1: MRT (Scene + Bright + Normal)
    draw_box(ax, cx, 5.9, bw, bh, 'MRT Forward Pass\n(G-Buffer / Main)', C_BLUE, fontsize=8)
    # MRT outputs
    draw_box(ax, 0.3, 5.9, 1.5, 0.55, 'Offscreen\nColor', C_TEAL, fontsize=7)
    draw_box(ax, 0.3, 5.2, 1.5, 0.55, 'Bright\nExtract', C_ORANGE, fontsize=7)
    draw_box(ax, 0.3, 4.5, 1.5, 0.55, 'Normal\nBuffer', C_PURPLE, fontsize=7)
    draw_arrow(ax, cx, 6.2, 1.8, 6.15, color=C_TEAL)
    draw_arrow(ax, cx, 6.1, 1.8, 5.45, color=C_ORANGE)
    draw_arrow(ax, cx, 6.0, 1.8, 4.75, color=C_PURPLE)
    draw_label(ax, cx+bw+0.3, 6.2, 'Opaque + Alpha-Mask\n+ Skybox + Skinned', fontsize=6.5)

    # Pass 2: SSR
    draw_box(ax, cx, 4.8, bw, bh, 'SSR Pass\n(ray-march)', C_PURPLE, fontsize=8)
    draw_label(ax, cx+bw+0.3, 5.1, 'Reads: Normal,\nDepth, Color', fontsize=6.5)

    # Pass 3: SSAO
    draw_box(ax, cx, 3.7, bw, bh, 'SSAO Pass\n(hemisphere)', C_GREEN, fontsize=8)
    draw_label(ax, cx+bw+0.3, 4.0, 'Reads: Depth,\nNormal', fontsize=6.5)

    # Pass 4-5: Bloom Blur
    draw_box(ax, cx, 2.6, bw, bh, 'Bloom Blur\nH → V (Gaussian)', C_ORANGE, fontsize=8)
    draw_label(ax, cx+bw+0.3, 2.9, 'Bright → Temp\n→ FinalBloom', fontsize=6.5)

    # Pass 6: Composite
    draw_box(ax, cx, 1.5, bw, bh, 'Composite Pass\n(+ Blending)', C_RED, fontsize=8)
    draw_label(ax, cx+bw+0.3, 1.8, 'Scene + Bloom + SSR\n+ SSAO + Transparent\n+ Particles + Debug', fontsize=6.5)

    # Pass 7: Speed Post-Process
    draw_box(ax, cx, 0.5, bw, bh, 'Speed / Death FX\n→ Final Scene', C_DARK, fontsize=8)
    draw_label(ax, cx+bw+0.3, 0.8, 'Radial blur\n+ desaturation', fontsize=6.5)

    # Pass 8: UI
    draw_box(ax, 8.2, 0.5, 2.0, bh, 'UI Pass\nImGui → Swapchain', '#E74C3C', fontsize=8)
    draw_arrow(ax, cx+bw, 0.82, 8.2, 0.82, color='#E74C3C', lw=1.5)

    # Vertical arrows (main flow)
    for y_top, y_bot in [(7.0, 6.55), (5.9, 5.45), (4.8, 4.35), (3.7, 3.25), (2.6, 2.15), (1.5, 1.15)]:
        draw_arrow(ax, cx + bw/2, y_top, cx + bw/2, y_bot, color=C_DARK, lw=1.5)

    # Synchronization note
    rect = FancyBboxPatch((8.0, 3.5), 2.8, 1.2, boxstyle="round,pad=0.1",
                          facecolor='#FFF3CD', edgecolor='#856404', linewidth=0.8, alpha=0.9)
    ax.add_patch(rect)
    ax.text(9.4, 4.1, 'Synchronisation\nvkCmdPipelineBarrier2\nimage_barrier() between\neach pass', ha='center',
            va='center', fontsize=6.5, color='#856404', style='italic')

    plt.tight_layout()
    path = os.path.join(OUT_DIR, 'render_pass_flowchart.png')
    fig.savefig(path, dpi=200, bbox_inches='tight', facecolor=BG_COLOR)
    plt.close(fig)
    print(f'  ✓ {path}')


# ════════════════════════════════════════════════════════════════
# DIAGRAM 3: Vehicle (Bicycle) Physics Setup
# Source: bikeController.hpp/cpp, Level1.cpp
# ════════════════════════════════════════════════════════════════
def generate_vehicle_diagram():
    fig, ax = plt.subplots(1, 1, figsize=(10, 7))
    ax.set_xlim(0, 10); ax.set_ylim(0, 7)
    ax.set_aspect('equal'); ax.axis('off')
    fig.patch.set_facecolor(BG_COLOR)
    ax.set_title('Bicycle Physics Controller: Jolt Integration', fontsize=TITLE_SIZE, fontweight='bold', pad=15)

    # ── Top: External inputs ──
    draw_box(ax, 0.3, 6.0, 2.0, 0.6, 'InputSystem\nActions: Forward,\nSteer, Jump, Pedal', C_BLUE, fontsize=7)
    draw_box(ax, 4.0, 6.0, 2.0, 0.6, 'JPH::PhysicsSystem\nBody Interface', C_GREEN, fontsize=7)
    draw_box(ax, 7.5, 6.0, 2.0, 0.6, 'GameplayState\nisAlive, engineForce\nbikeSpeed, Yaw', C_ORANGE, fontsize=7)

    # ── Core: BikeController ──
    draw_box(ax, 2.5, 4.3, 5.0, 1.2, 'BikeController::Update(dt)', C_DARK, fontsize=10)

    # Arrows into controller
    draw_arrow(ax, 1.3, 6.0, 3.5, 5.55, color=C_BLUE, lw=1.3)
    draw_arrow(ax, 5.0, 6.0, 5.0, 5.55, color=C_GREEN, lw=1.3)
    draw_arrow(ax, 8.5, 6.0, 6.5, 5.55, color=C_ORANGE, lw=1.3)

    # ── Sub-systems inside controller ──
    subsys = [
        (0.2, 2.8, 2.2, 'Ground Detection\nRayCast -1.8Y\nNarrowPhaseQuery', C_TEAL),
        (2.7, 2.8, 2.2, 'Steering Model\nAckermann (tan θ)\nwheelBase = 1.6', C_BLUE),
        (5.2, 2.8, 2.2, 'Lean Dynamics\nmaxLean = 40°\nleanSpeed = 90°/s', C_PURPLE),
        (7.7, 2.8, 2.2, 'Pedal Mashing\nAlternating L/R\npedalBurst = 800N', C_RED),
    ]
    for sx, sy, sw, txt, col in subsys:
        draw_box(ax, sx, sy, sw, 0.75, txt, col, fontsize=7)
        draw_arrow(ax, sx + sw/2, 4.3, sx + sw/2, 3.6, lw=0.8, color=C_GRAY)

    # ── Output: Forces applied ──
    draw_label(ax, 5.0, 2.2, 'Forces & Rotation Applied to Jolt Body', fontsize=9, color=C_DARK)

    outputs = [
        (0.5, 1.2, 'Jump Impulse\nvel.Y += 16', C_TEAL),
        (2.5, 1.2, 'Yaw Quaternion\nyaw + pitch + lean', C_BLUE),
        (4.8, 1.2, 'Lateral Grip\n5000 N stiffness', C_PURPLE),
        (7.2, 1.2, 'Engine Force\nmax 3000 + 20×v', C_RED),
    ]
    for ox, oy, txt, col in outputs:
        draw_box(ax, ox, oy, 2.0, 0.65, txt, col, fontsize=7)

    for ox in [1.3, 3.3, 5.3, 7.7]:
        draw_arrow(ax, ox, 2.8, ox, 1.9, lw=0.8, color=C_GRAY)

    # ── Bottom: Death mechanic ──
    draw_box(ax, 3.0, 0.1, 4.0, 0.6, 'Death Check: speed < 3.5 && pitch > 10°\n→ isAlive=false, damping reset, ragdoll', '#E74C3C', fontsize=7)
    draw_arrow(ax, 5.0, 1.2, 5.0, 0.75, lw=1.0, color='#E74C3C')

    plt.tight_layout()
    path = os.path.join(OUT_DIR, 'vehicle_constraint_setup.png')
    fig.savefig(path, dpi=200, bbox_inches='tight', facecolor=BG_COLOR)
    plt.close(fig)
    print(f'  ✓ {path}')


# ════════════════════════════════════════════════════════════════
# DIAGRAM 4: Vulkan Asset Upload Pipeline
# Source: vkimage.cpp (load_image_texture2d), RenderSystem.hpp (UploadSingleMesh)
# ════════════════════════════════════════════════════════════════
def generate_asset_upload_diagram():
    fig, ax = plt.subplots(1, 1, figsize=(11, 6))
    ax.set_xlim(0, 11); ax.set_ylim(0, 6)
    ax.set_aspect('equal'); ax.axis('off')
    fig.patch.set_facecolor(BG_COLOR)
    ax.set_title('Vulkan Asset Upload Pipeline: Staging to Device-Local', fontsize=TITLE_SIZE, fontweight='bold', pad=15)

    # ── Row 1: Mesh Upload (UploadSingleMesh) ──
    draw_label(ax, 5.5, 5.6, 'Mesh Upload Path  (RenderSystem::UploadSingleMesh)', fontsize=9, color=C_DARK)

    draw_box(ax, 0.2, 4.7, 1.8, 0.7, 'CPU Mesh Data\npositions[]\ntexcoords[]\nnormals[]\nindices[]', C_GRAY, fontsize=6.5)
    draw_box(ax, 2.5, 4.7, 2.0, 0.7, 'Staging Buffers\nHOST_VISIBLE\nvmaMapMemory\nmemcpy → Unmap', C_ORANGE, fontsize=6.5)
    draw_box(ax, 5.0, 4.7, 2.0, 0.7, 'vkCmdCopyBuffer\nONE_TIME_SUBMIT\nTransfer Queue', C_BLUE, fontsize=6.5)
    draw_box(ax, 7.5, 4.7, 2.2, 0.7, 'GPU VBO / IBO\nDEVICE_LOCAL\nbuffer_barrier()\nVERTEX_ATTR_INPUT', C_GREEN, fontsize=6.5)
    draw_box(ax, 10.0, 4.7, 0.8, 0.7, 'Fence\nWait\nIdle', C_RED, fontsize=6.5)

    for x1, x2 in [(2.0, 2.5), (4.5, 5.0), (7.0, 7.5), (9.7, 10.0)]:
        draw_arrow(ax, x1, 5.05, x2, 5.05, color=C_DARK, lw=1.5)

    # ── Row 2: Image/Texture Upload (load_image_texture2d) ──
    draw_label(ax, 5.5, 3.8, 'Texture Upload Path  (labut2::load_image_texture2d)', fontsize=9, color=C_DARK)

    draw_box(ax, 0.2, 2.7, 1.8, 0.8, 'Raw Pixels\n(stb_image)\nRGBA 8-bit\nwidth × height', C_GRAY, fontsize=6.5)
    draw_box(ax, 2.5, 2.7, 2.0, 0.8, 'Staging Buffer\nHOST_VISIBLE\nvmaMapMemory\nmemcpy pixels', C_ORANGE, fontsize=6.5)
    draw_box(ax, 5.0, 2.7, 2.0, 0.8, 'Layout Transition\nUNDEFINED →\nTRANSFER_DST\nvkCmdCopyBuffer\nToImage', C_BLUE, fontsize=6.5)
    draw_box(ax, 7.5, 2.7, 2.2, 0.8, 'Mipmap Gen.\nvkCmdBlitImage\nper level ÷2\n+ final barrier', C_PURPLE, fontsize=6.5)
    draw_box(ax, 10.0, 2.7, 0.8, 0.8, 'SHADER\nREAD\nONLY', C_GREEN, fontsize=6.5)

    for x1, x2 in [(2.0, 2.5), (4.5, 5.0), (7.0, 7.5), (9.7, 10.0)]:
        draw_arrow(ax, x1, 3.1, x2, 3.1, color=C_DARK, lw=1.5)

    # ── Row 3: Material Registration ──
    draw_label(ax, 5.5, 1.9, 'Material Descriptor Path  (RegisterModelAssets)', fontsize=9, color=C_DARK)

    draw_box(ax, 0.2, 0.9, 1.8, 0.7, 'GLB Parser\nEngineModel\ntextures[]\nmaterials[]\nmeshes[]', C_GRAY, fontsize=6.5)
    draw_box(ax, 2.5, 0.9, 2.0, 0.7, 'Reindex\nbaseTexIdx +=\nbaseMeshIdx +=\nbaseMatIdx +=', C_ORANGE, fontsize=6.5)
    draw_box(ax, 5.0, 0.9, 2.0, 0.7, 'Descriptor Alloc\nAddOneMaterial\nDescriptor()\nper-material set', C_BLUE, fontsize=6.5)
    draw_box(ax, 7.5, 0.9, 2.2, 0.7, 'Flecs Entity\nMeshComponent\nMaterialComp.\nLocalTransform', C_TEAL, fontsize=6.5)

    for x1, x2 in [(2.0, 2.5), (4.5, 5.0), (7.0, 7.5)]:
        draw_arrow(ax, x1, 1.25, x2, 1.25, color=C_DARK, lw=1.5)

    # Sync note
    rect = FancyBboxPatch((0.2, 0.05), 10.6, 0.55, boxstyle="round,pad=0.08",
                          facecolor='#FFF3CD', edgecolor='#856404', linewidth=0.7, alpha=0.7)
    ax.add_patch(rect)
    ax.text(5.5, 0.32, 'All transfers use one-shot command buffers (ONE_TIME_SUBMIT) + vkQueueWaitIdle() synchronisation. '
            'Staging buffers are RAII-destroyed after transfer completes.',
            ha='center', va='center', fontsize=6.5, color='#856404', style='italic')

    plt.tight_layout()
    path = os.path.join(OUT_DIR, 'asset_upload_pipeline.png')
    fig.savefig(path, dpi=200, bbox_inches='tight', facecolor=BG_COLOR)
    plt.close(fig)
    print(f'  ✓ {path}')


# ════════════════════════════════════════════════════════════════
if __name__ == '__main__':
    print('Generating diagrams...')
    generate_flecs_diagram()
    generate_render_pass_diagram()
    generate_vehicle_diagram()
    generate_asset_upload_diagram()
    print('Done. All PNGs saved to figures/')
