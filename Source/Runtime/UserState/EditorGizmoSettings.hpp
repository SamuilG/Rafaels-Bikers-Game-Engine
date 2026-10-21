#pragma once

namespace engine {
enum class EditorTransformTool { Select, Move, Rotate, Scale };

struct EditorGizmoSettings {
    EditorTransformTool tool = EditorTransformTool::Move;
    bool localSpace = false;
    bool snap = false;
    float moveSnap = 1.0f;
    float rotateSnap = 15.0f;
    float scaleSnap = 0.1f;
    bool visible = true; // Component guides; transform handles remain independent.
    bool selectedOnly = true;
    bool lightIcons = true;
    bool lightGuides = true;
    bool particleIcons = true;
    float iconSize = 24.0f; // Fixed screen pixels, independent of camera distance.
    float handleSize = 0.12f;
};
}
