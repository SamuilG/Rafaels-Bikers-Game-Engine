#include "Runtime/UI/EditorTransform.hpp"

#include <imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

glm::mat4 Compose(const glm::vec3& translation, const glm::vec3& rotation,
                  const glm::vec3& scale)
{
    glm::mat4 matrix(1.0f);
    ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(translation),
        glm::value_ptr(rotation), glm::value_ptr(scale), glm::value_ptr(matrix));
    return matrix;
}

void RequireSameMatrix(const glm::mat4& expected, const glm::mat4& actual,
                       const std::string& name)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
        {
            const float tolerance = 2e-5f * (1.0f + std::abs(expected[column][row]));
            Require(std::isfinite(actual[column][row]) &&
                    std::abs(expected[column][row] - actual[column][row]) <= tolerance,
                    name + ": matrix mismatch at " + std::to_string(column) +
                        "," + std::to_string(row));
        }
}

void CheckRoundTrip(const std::string& name, const glm::mat4& matrix)
{
    glm::vec3 translation, rotation, scale;
    Require(engine::editor_transform::Decompose(matrix, glm::value_ptr(translation),
        glm::value_ptr(rotation), glm::value_ptr(scale)), name + ": decomposition failed");
    RequireSameMatrix(matrix, Compose(translation, rotation, scale), name);
    Require((glm::determinant(glm::mat3(matrix)) < 0.0f) ==
            (scale.x * scale.y * scale.z < 0.0f), name + ": reflection was lost");
}

void CheckRejected(const std::string& name, const glm::mat4& matrix)
{
    float translation[3] = { 11.0f, 12.0f, 13.0f };
    float rotation[3] = { 21.0f, 22.0f, 23.0f };
    float scale[3] = { 31.0f, 32.0f, 33.0f };
    Require(!engine::editor_transform::Decompose(matrix, translation, rotation, scale),
            name + ": unsupported matrix was accepted");
    for (int axis = 0; axis < 3; ++axis)
        Require(translation[axis] == 11.0f + axis &&
                rotation[axis] == 21.0f + axis && scale[axis] == 31.0f + axis,
                name + ": failed decomposition changed the editor cache");
}
}

int main()
{
    try
    {
        CheckRoundTrip("identity", glm::mat4(1.0f));
        CheckRoundTrip("ordinary TRS", Compose({ 7, -5, 3 }, { 21, -38, 79 }, { 2, 3, 4 }));
        for (int axis = 0; axis < 3; ++axis)
        {
            glm::vec3 scale(2, 3, 4);
            scale[axis] = -scale[axis];
            CheckRoundTrip("single negative axis " + std::to_string(axis),
                           Compose({ 1, 2, 3 }, { 0, 0, 0 }, scale));
            CheckRoundTrip("rotated reflection " + std::to_string(axis),
                           Compose({ -11, 5, 2 }, { 29, -51, 121 }, scale));
        }
        CheckRoundTrip("three negative axes", Compose({ 2, 3, 4 }, { 31, 64, -48 }, { -2, -3, -4 }));
        CheckRoundTrip("two negative axes", Compose({ 2, 3, 4 }, { -41, 14, 28 }, { -2, 3, -4 }));
        for (float pitch : { -90.0f, -89.99999f, -89.99f, 89.99f, 89.99999f, 90.0f })
            for (const glm::vec3 scale : { glm::vec3(2, 3, 4), glm::vec3(-2, 3, 4) })
                CheckRoundTrip("Euler singularity " + std::to_string(pitch),
                               Compose({ 1, 2, 3 }, { 32, pitch, -17 }, scale));

        // Refreshing the Inspector each frame, then editing just translation,
        // must not turn a mirrored entity into an unmirrored one.
        const glm::mat4 mirrored = Compose({ 5, 6, 7 }, { -25, 42, 83 }, { -2, 3, 4 });
        glm::mat4 edited = mirrored;
        for (int frame = 0; frame < 100; ++frame)
        {
            glm::vec3 translation, rotation, scale;
            Require(engine::editor_transform::Decompose(edited, glm::value_ptr(translation),
                glm::value_ptr(rotation), glm::value_ptr(scale)), "repeated Inspector refresh failed");
            translation.x += 0.5f;
            edited = Compose(translation, rotation, scale);
        }
        glm::mat4 expected = mirrored;
        expected[3][0] += 50.0f;
        RequireSameMatrix(expected, edited, "repeated mirrored Inspector edits");

        for (int axis = 0; axis < 3; ++axis)
        {
            glm::mat4 singular(1.0f);
            singular[axis][axis] = 0.0f;
            CheckRejected("zero scale axis " + std::to_string(axis), singular);
        }
        CheckRejected("zero matrix", glm::mat4(0.0f));
        glm::mat4 invalid(1.0f);
        invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
        CheckRejected("NaN", invalid);
        invalid[0][0] = std::numeric_limits<float>::infinity();
        CheckRejected("infinity", invalid);
        invalid = glm::mat4(1.0f);
        invalid[1][0] = 0.25f;
        CheckRejected("shear", invalid);
        invalid = glm::mat4(1.0f);
        invalid[0][3] = 0.25f;
        CheckRejected("perspective", invalid);
        std::cout << "PASS: signed TRS, all reflection axes, rotated mirrors, Euler singularities, "
                     "repeated Inspector edits, and invalid-input safety (real ImGuizmo).\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
