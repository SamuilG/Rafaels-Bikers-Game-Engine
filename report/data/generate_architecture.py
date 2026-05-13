#!/usr/bin/env python3
"""
Generate a high-level engine architecture block diagram from EngineUML.xml.
Parses the draw.io XML to extract core nodes and edges, then renders a clean,
academic-style block diagram using matplotlib.

Output: report/simplified_architecture.png
"""

import xml.etree.ElementTree as ET
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyArrowPatch
import numpy as np

# ---------------------------------------------------------------------------
# 1. Parse the XML and extract core nodes + edges
# ---------------------------------------------------------------------------

tree = ET.parse('EngineUML.xml')
root = tree.getroot()

# Find all mxCell elements
cells = {}
edges = []

for cell in root.iter('{http://www.w3.org/1999/xhtml}mxCell'):
    pass  # draw.io doesn't use namespaces this way

# draw.io XML uses plain mxCell tags
for cell in root.iter('mxCell'):
    cid = cell.get('id', '')
    value = cell.get('value', '')
    parent = cell.get('parent', '')
    source = cell.get('source', '')
    target = cell.get('target', '')
    edge = cell.get('edge', '')
    style = cell.get('style', '')

    if value:
        cells[cid] = {
            'value': value.strip(),
            'parent': parent,
            'style': style or ''
        }

    if edge == '1' and source and target:
        edges.append((source, target))

# Also check UserObject elements (draw.io sometimes wraps cells this way)
for uo in root.iter('UserObject'):
    cid = uo.get('id', '')
    for cell in uo.iter('mxCell'):
        source = cell.get('source', '')
        target = cell.get('target', '')
        edge_flag = cell.get('edge', '')
        if edge_flag == '1' and source and target:
            edges.append((source, target))

# Identify the core "swimlane" nodes (top-level class boxes)
core_classes = {}
for cid, info in cells.items():
    if 'swimlane' in info.get('style', ''):
        name = info['value'].replace('&nbsp;', '').strip()
        if name:
            core_classes[cid] = name

# Filter to only the macro-architecture nodes we care about
CORE_NODES = {
    'EngineApplication', 'World', 'RenderSystem', 'PhysicsSystem',
    'InputManager', 'Entity', 'CameraSystem', 'TerrainSystem',
    'AssetManager', 'ImGuiLayer', 'WindowSystem', 'VehicleController',
    'RenderGraph', 'RHI_Device'
}

# Build a mapping from cell ID to canonical class name
id_to_class = {}
for cid, name in core_classes.items():
    # Normalize: strip trailing spaces
    clean = name.strip()
    # Match against our core set
    for cn in CORE_NODES:
        if cn.lower().replace('_', '') in clean.lower().replace('_', '').replace(' ', ''):
            id_to_class[cid] = cn
            break

# Also map child cells to their parent class
child_to_class = {}
for cid, info in cells.items():
    parent = info.get('parent', '')
    if parent in id_to_class:
        child_to_class[cid] = id_to_class[parent]

# Build edge list between core classes
core_edges = set()
for source, target in edges:
    src_class = id_to_class.get(source) or child_to_class.get(source)
    tgt_class = id_to_class.get(target) or child_to_class.get(target)
    if src_class and tgt_class and src_class != tgt_class:
        core_edges.add((src_class, tgt_class))

# ---------------------------------------------------------------------------
# 2. Define the simplified high-level diagram layout
# ---------------------------------------------------------------------------
# We want ONLY the essential macro-architecture, filtering out low-level noise
# like RHI_Device, RenderGraph, RenderPass, RHI_Texture, ShadowPass, etc.

# Final node set: the absolute core
DISPLAY_NODES = [
    'EngineApplication',
    'WindowSystem',
    'InputManager',
    'RenderSystem',
    'PhysicsSystem',
    'World',
    'AssetManager',
    'Entity',
    'CameraSystem',
    'TerrainSystem',
    'VehicleController',
    'ImGuiLayer',
]

# Manual positions: layered top-down layout (x, y) for academic clarity
# Row 0: EngineApplication (top)
# Row 1: Its direct subsystems
# Row 2: Sub-subsystems
# Row 3: Leaf nodes
positions = {
    'EngineApplication':   (0.50, 0.92),
    'WindowSystem':        (0.85, 0.75),
    'InputManager':        (0.15, 0.75),
    'RenderSystem':        (0.72, 0.58),
    'PhysicsSystem':       (0.28, 0.58),
    'World':               (0.50, 0.58),
    'ImGuiLayer':          (0.15, 0.40),
    'AssetManager':        (0.85, 0.40),
    'CameraSystem':        (0.38, 0.38),
    'TerrainSystem':       (0.62, 0.38),
    'Entity':              (0.50, 0.18),
    'VehicleController':   (0.28, 0.18),
}

# Directed edges to display: ownership / data-flow
DISPLAY_EDGES = [
    ('EngineApplication', 'RenderSystem'),
    ('EngineApplication', 'PhysicsSystem'),
    ('EngineApplication', 'InputManager'),
    ('EngineApplication', 'World'),
    ('EngineApplication', 'ImGuiLayer'),
    ('RenderSystem', 'WindowSystem'),
    ('RenderSystem', 'AssetManager'),
    ('World', 'CameraSystem'),
    ('World', 'TerrainSystem'),
    ('World', 'Entity'),
    ('PhysicsSystem', 'VehicleController'),
    ('Entity', 'VehicleController'),
]

# Node display labels
LABELS = {
    'EngineApplication': 'Engine\nApplication',
    'WindowSystem': 'Window\nSystem',
    'InputManager': 'Input\nManager',
    'RenderSystem': 'Render\nSystem',
    'PhysicsSystem': 'Physics\nSystem',
    'World': 'World\n(Scene)',
    'AssetManager': 'Asset\nManager',
    'Entity': 'Entity',
    'CameraSystem': 'Camera\nSystem',
    'TerrainSystem': 'Terrain\nSystem',
    'VehicleController': 'Vehicle\nController',
    'ImGuiLayer': 'ImGui\nLayer',
}

# Color coding by subsystem category
COLORS = {
    'EngineApplication': '#2C3E50',   # Dark blue-grey (core)
    'WindowSystem':      '#2980B9',   # Blue (platform)
    'InputManager':      '#2980B9',   # Blue (platform)
    'RenderSystem':      '#E74C3C',   # Red (rendering)
    'PhysicsSystem':     '#27AE60',   # Green (physics)
    'World':             '#8E44AD',   # Purple (scene)
    'AssetManager':      '#E74C3C',   # Red (rendering)
    'Entity':            '#8E44AD',   # Purple (scene)
    'CameraSystem':      '#8E44AD',   # Purple (scene)
    'TerrainSystem':     '#8E44AD',   # Purple (scene)
    'VehicleController': '#27AE60',   # Green (physics)
    'ImGuiLayer':        '#F39C12',   # Orange (UI)
}

# ---------------------------------------------------------------------------
# 3. Render the diagram
# ---------------------------------------------------------------------------

fig, ax = plt.subplots(1, 1, figsize=(10, 8), dpi=200)
ax.set_xlim(-0.05, 1.05)
ax.set_ylim(0.05, 1.02)
ax.set_aspect('equal')
ax.axis('off')

BOX_W = 0.13
BOX_H = 0.065

# Draw nodes
for node in DISPLAY_NODES:
    x, y = positions[node]
    color = COLORS[node]
    label = LABELS[node]

    # Draw rectangle
    rect = mpatches.FancyBboxPatch(
        (x - BOX_W/2, y - BOX_H/2), BOX_W, BOX_H,
        boxstyle="round,pad=0.008",
        facecolor=color,
        edgecolor='#1a1a1a',
        linewidth=1.2,
        alpha=0.92,
        zorder=3,
    )
    ax.add_patch(rect)

    # Label
    ax.text(x, y, label,
            ha='center', va='center',
            fontsize=7.5, fontweight='bold',
            color='white',
            fontfamily='sans-serif',
            zorder=4)

# Draw edges with arrows
def get_connection_point(src_pos, dst_pos, box_w, box_h):
    """Compute edge attachment points on box boundaries."""
    sx, sy = src_pos
    dx, dy = dst_pos

    # Direction vector
    vx = dx - sx
    vy = dy - sy
    length = np.sqrt(vx**2 + vy**2)
    if length == 0:
        return src_pos, dst_pos

    # Normalize
    nvx, nvy = vx / length, vy / length

    # Source exit point
    hw, hh = box_w / 2, box_h / 2
    # Try horizontal edge
    if abs(nvx) > 1e-6:
        t_x = hw / abs(nvx)
    else:
        t_x = float('inf')
    if abs(nvy) > 1e-6:
        t_y = hh / abs(nvy)
    else:
        t_y = float('inf')
    t_src = min(t_x, t_y)
    src_out = (sx + nvx * t_src, sy + nvy * t_src)

    # Destination entry point (reverse direction)
    if abs(nvx) > 1e-6:
        t_x = hw / abs(nvx)
    else:
        t_x = float('inf')
    if abs(nvy) > 1e-6:
        t_y = hh / abs(nvy)
    else:
        t_y = float('inf')
    t_dst = min(t_x, t_y)
    dst_in = (dx - nvx * t_dst, dy - nvy * t_dst)

    return src_out, dst_in


for src, dst in DISPLAY_EDGES:
    src_pos = positions[src]
    dst_pos = positions[dst]
    start, end = get_connection_point(src_pos, dst_pos, BOX_W, BOX_H)

    ax.annotate(
        '', xy=end, xytext=start,
        arrowprops=dict(
            arrowstyle='-|>',
            color='#555555',
            lw=1.3,
            shrinkA=1,
            shrinkB=1,
            connectionstyle='arc3,rad=0.0',
        ),
        zorder=2,
    )

# Legend
legend_items = [
    ('Core',       '#2C3E50'),
    ('Platform',   '#2980B9'),
    ('Rendering',  '#E74C3C'),
    ('Physics',    '#27AE60'),
    ('Scene',      '#8E44AD'),
    ('UI',         '#F39C12'),
]
legend_patches = [
    mpatches.Patch(facecolor=c, edgecolor='#1a1a1a', label=l)
    for l, c in legend_items
]
ax.legend(
    handles=legend_patches,
    loc='lower right',
    fontsize=7,
    framealpha=0.9,
    edgecolor='#cccccc',
    title='Subsystem',
    title_fontsize=8,
)

# Title
ax.text(0.50, 0.995, 'Steer Engine: Macro-Architecture',
        ha='center', va='top',
        fontsize=12, fontweight='bold',
        fontfamily='sans-serif',
        color='#222222')

plt.tight_layout(pad=0.5)
plt.savefig('simplified_architecture.png', dpi=200, bbox_inches='tight',
            facecolor='white', edgecolor='none')
print("✓ Saved simplified_architecture.png")
