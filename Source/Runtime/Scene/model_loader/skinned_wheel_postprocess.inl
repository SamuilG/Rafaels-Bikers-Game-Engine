
namespace engine::detail {

namespace {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains_wheel_keyword(const std::string& value) {
    const std::string lowered = to_lower_copy(value);
    return lowered.find("wheel") != std::string::npos
        || lowered.find("tire") != std::string::npos
        || lowered.find("tyre") != std::string::npos
        || lowered.find("rim") != std::string::npos;
}

const uint8_t* accessor_ptr(const tinygltf::Model& model, const tinygltf::Accessor& accessor) {
    const auto& buffer_view = model.bufferViews[accessor.bufferView];
    const auto& buffer = model.buffers[buffer_view.buffer];
    return buffer.data.data() + buffer_view.byteOffset + accessor.byteOffset;
}

size_t accessor_stride(const tinygltf::Model& model, const tinygltf::Accessor& accessor) {
    const auto& buffer_view = model.bufferViews[accessor.bufferView];
    if (buffer_view.byteStride > 0) {
        return buffer_view.byteStride;
    }
    return tinygltf::GetComponentSizeInBytes(accessor.componentType)
        * tinygltf::GetNumComponentsInType(accessor.type);
}

glm::mat4 get_node_transform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        return glm::make_mat4(node.matrix.data());
    }

    glm::mat4 translation = glm::mat4(1.0f);
    glm::mat4 rotation = glm::mat4(1.0f);
    glm::mat4 scale = glm::mat4(1.0f);

    if (node.translation.size() == 3) {
        translation = glm::translate(glm::mat4(1.0f),
            glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
    }
    if (node.rotation.size() == 4) {
        rotation = glm::mat4_cast(glm::make_quat(node.rotation.data()));
    }
    if (node.scale.size() == 3) {
        scale = glm::scale(glm::mat4(1.0f),
            glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    }
    return translation * rotation * scale;
}

void compute_global_transforms_recursive(
    const tinygltf::Model& model,
    int node_index,
    const glm::mat4& parent,
    std::vector<glm::mat4>& out_globals)
{
    if (node_index < 0 || node_index >= static_cast<int>(model.nodes.size())) {
        return;
    }

    const tinygltf::Node& node = model.nodes[node_index];
    const glm::mat4 global = parent * get_node_transform(node);
    out_globals[node_index] = global;

    for (int child : node.children) {
        compute_global_transforms_recursive(model, child, global, out_globals);
    }
}

std::vector<glm::mat4> compute_global_transforms(const tinygltf::Model& model) {
    std::vector<glm::mat4> globals(model.nodes.size(), glm::mat4(1.0f));
    if (model.scenes.empty()) {
        return globals;
    }

    const int scene_index = model.defaultScene > -1 ? model.defaultScene : 0;
    const tinygltf::Scene& scene = model.scenes[scene_index];
    for (int root : scene.nodes) {
        compute_global_transforms_recursive(model, root, glm::mat4(1.0f), globals);
    }
    return globals;
}

std::vector<glm::mat4> load_inverse_bind_matrices(const tinygltf::Model& model, int accessor_index) {
    std::vector<glm::mat4> matrices;
    if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) {
        return matrices;
    }

    const auto& accessor = model.accessors[accessor_index];
    const uint8_t* data = accessor_ptr(model, accessor);
    const size_t stride = accessor_stride(model, accessor);
    matrices.resize(accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        matrices[i] = glm::make_mat4(reinterpret_cast<const float*>(data + i * stride));
    }
    return matrices;
}

uint32_t read_index(const uint8_t* data, int component_type, size_t index) {
    switch (component_type) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        return reinterpret_cast<const uint32_t*>(data)[index];
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return reinterpret_cast<const uint16_t*>(data)[index];
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return reinterpret_cast<const uint8_t*>(data)[index];
    default:
        return 0;
    }
}

std::vector<uint32_t> load_indices(const tinygltf::Model& model, int accessor_index) {
    std::vector<uint32_t> indices;
    if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) {
        return indices;
    }

    const auto& accessor = model.accessors[accessor_index];
    const uint8_t* data = accessor_ptr(model, accessor);
    indices.reserve(accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        indices.push_back(read_index(data, accessor.componentType, i));
    }
    return indices;
}

std::vector<uint16_t> load_dominant_joints(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive)
{
    std::vector<uint16_t> dominant_joints;
    auto joints_it = primitive.attributes.find("JOINTS_0");
    auto weights_it = primitive.attributes.find("WEIGHTS_0");
    if (joints_it == primitive.attributes.end() || weights_it == primitive.attributes.end()) {
        return dominant_joints;
    }

    const auto& joints_accessor = model.accessors[joints_it->second];
    const auto& weights_accessor = model.accessors[weights_it->second];
    const uint8_t* joints_data = accessor_ptr(model, joints_accessor);
    const uint8_t* weights_data = accessor_ptr(model, weights_accessor);
    const size_t joints_stride = accessor_stride(model, joints_accessor);
    const size_t weights_stride = accessor_stride(model, weights_accessor);

    dominant_joints.resize(joints_accessor.count);
    for (size_t i = 0; i < joints_accessor.count; ++i) {
        const uint8_t* joint_ptr = joints_data + i * joints_stride;
        const float* weight_ptr = reinterpret_cast<const float*>(weights_data + i * weights_stride);

        uint16_t best_joint = joint_ptr[0];
        float best_weight = weight_ptr[0];
        for (int k = 1; k < 4; ++k) {
            if (weight_ptr[k] > best_weight) {
                best_weight = weight_ptr[k];
                best_joint = joint_ptr[k];
            }
        }
        dominant_joints[i] = best_joint;
    }

    return dominant_joints;
}

struct SplitWheelMesh {
    EngineMesh mesh;
    glm::vec3 localCenter{ 0.0f, 0.0f, 0.0f };
};

SplitWheelMesh build_submesh_for_joint(
    const EngineMesh& source,
    const std::vector<uint32_t>& triangle_indices,
    const std::string& name_suffix)
{
    SplitWheelMesh result;
    EngineMesh& submesh = result.mesh;
    submesh.name = source.name + "_" + name_suffix;
    submesh.materialIndex = source.materialIndex;
    submesh.hasSkinning = false;

    if (triangle_indices.empty()) {
        return result;
    }

    std::unordered_map<uint32_t, uint32_t> remap;
    remap.reserve(triangle_indices.size());
    glm::vec3 minPos(std::numeric_limits<float>::max());
    glm::vec3 maxPos(std::numeric_limits<float>::lowest());

    for (uint32_t original_index : triangle_indices) {
        minPos = glm::min(minPos, source.positions[original_index]);
        maxPos = glm::max(maxPos, source.positions[original_index]);
    }
    result.localCenter = (minPos + maxPos) * 0.5f;

    for (uint32_t original_index : triangle_indices) {
        auto [it, inserted] = remap.emplace(original_index, static_cast<uint32_t>(remap.size()));
        const uint32_t new_index = it->second;
        if (inserted) {
            submesh.positions.push_back(source.positions[original_index] - result.localCenter);
            if (!source.normals.empty()) {
                submesh.normals.push_back(source.normals[original_index]);
            }
            if (!source.texcoords.empty()) {
                submesh.texcoords.push_back(source.texcoords[original_index]);
            }
        }
        submesh.indices.push_back(new_index);
    }

    return result;
}

EngineMesh build_remaining_mesh(
    const EngineMesh& source,
    const std::vector<uint32_t>& triangle_indices)
{
    EngineMesh out_mesh;
    out_mesh.name = source.name;
    out_mesh.materialIndex = source.materialIndex;
    out_mesh.hasSkinning = source.hasSkinning;

    if (triangle_indices.empty()) {
        return out_mesh;
    }

    std::unordered_map<uint32_t, uint32_t> remap;
    remap.reserve(triangle_indices.size());
    for (uint32_t original_index : triangle_indices) {
        auto [it, inserted] = remap.emplace(original_index, static_cast<uint32_t>(remap.size()));
        const uint32_t new_index = it->second;
        if (inserted) {
            out_mesh.positions.push_back(source.positions[original_index]);
            if (!source.normals.empty()) {
                out_mesh.normals.push_back(source.normals[original_index]);
            }
            if (!source.texcoords.empty()) {
                out_mesh.texcoords.push_back(source.texcoords[original_index]);
            }
        }
        out_mesh.indices.push_back(new_index);
    }

    return out_mesh;
}

} // namespace

void postprocess_skinned_wheels(const tinygltf::Model& gltf, EngineModel& model) {
    if (gltf.nodes.empty() || gltf.meshes.empty() || gltf.skins.empty()) {
        return;
    }

    const std::vector<glm::mat4> global_transforms = compute_global_transforms(gltf);

    std::unordered_map<std::string, int> node_by_name;
    node_by_name.reserve(gltf.nodes.size());
    for (int i = 0; i < static_cast<int>(gltf.nodes.size()); ++i) {
        if (!gltf.nodes[i].name.empty()) {
            node_by_name.emplace(gltf.nodes[i].name, i);
        }
    }

    std::vector<EngineInstance> extra_instances;
    std::vector<EngineMesh> extra_meshes;
    std::vector<size_t> remove_instance_indices;

    for (size_t instance_index = 0; instance_index < model.scenes.size(); ++instance_index) {
        EngineInstance& instance = model.scenes[instance_index];
        if (instance.meshIndex >= model.meshes.size()) {
            continue;
        }

        EngineMesh& mesh = model.meshes[instance.meshIndex];
        if (!mesh.hasSkinning) {
            continue;
        }

        auto node_it = node_by_name.find(instance.name);
        if (node_it == node_by_name.end()) {
            continue;
        }

        const int node_index = node_it->second;
        const tinygltf::Node& node = gltf.nodes[node_index];
        if (node.skin < 0 || node.skin >= static_cast<int>(gltf.skins.size()) || node.mesh < 0 || node.mesh >= static_cast<int>(gltf.meshes.size())) {
            continue;
        }

        const tinygltf::Skin& skin = gltf.skins[node.skin];
        if (skin.inverseBindMatrices < 0) {
            continue;
        }

        const tinygltf::Primitive& primitive = gltf.meshes[node.mesh].primitives.front();
        const std::vector<uint16_t> dominant_joints = load_dominant_joints(gltf, primitive);
        const std::vector<uint32_t> source_indices = load_indices(gltf, primitive.indices);
        if (dominant_joints.empty() || source_indices.empty()) {
            continue;
        }

        std::unordered_map<uint16_t, std::vector<uint32_t>> wheel_triangles;
        std::vector<uint32_t> remaining_triangles;
        remaining_triangles.reserve(source_indices.size());

        for (size_t tri = 0; tri + 2 < source_indices.size(); tri += 3) {
            const uint32_t i0 = source_indices[tri];
            const uint32_t i1 = source_indices[tri + 1];
            const uint32_t i2 = source_indices[tri + 2];
            const uint16_t j0 = dominant_joints[i0];
            const uint16_t j1 = dominant_joints[i1];
            const uint16_t j2 = dominant_joints[i2];

            if (j0 == j1 && j1 == j2
                && j0 < skin.joints.size()
                && contains_wheel_keyword(gltf.nodes[skin.joints[j0]].name)) {
                auto& bucket = wheel_triangles[j0];
                bucket.push_back(i0);
                bucket.push_back(i1);
                bucket.push_back(i2);
            } else {
                remaining_triangles.push_back(i0);
                remaining_triangles.push_back(i1);
                remaining_triangles.push_back(i2);
            }
        }

        if (wheel_triangles.empty()) {
            continue;
        }

        for (const auto& [joint_index, tri_indices] : wheel_triangles) {
            const int joint_node_index = skin.joints[joint_index];
            const tinygltf::Node& joint_node = gltf.nodes[joint_node_index];

            SplitWheelMesh split = build_submesh_for_joint(
                mesh,
                tri_indices,
                joint_node.name);
            if (split.mesh.indices.empty()) {
                continue;
            }

            const uint32_t new_mesh_index = static_cast<uint32_t>(model.meshes.size() + extra_meshes.size());
            extra_meshes.push_back(std::move(split.mesh));

            EngineInstance wheel_instance = instance;
            wheel_instance.meshIndex = new_mesh_index;
            wheel_instance.transform = instance.transform * glm::translate(glm::mat4(1.0f), split.localCenter);
            wheel_instance.name = joint_node.name;
            extra_instances.push_back(std::move(wheel_instance));
        }

        if (remaining_triangles.empty()) {
            remove_instance_indices.push_back(instance_index);
        } else {
            mesh = build_remaining_mesh(mesh, remaining_triangles);
        }
    }

    if (extra_meshes.empty()) {
        return;
    }

    model.meshes.insert(model.meshes.end(), extra_meshes.begin(), extra_meshes.end());

    if (!remove_instance_indices.empty()) {
        std::sort(remove_instance_indices.begin(), remove_instance_indices.end(), std::greater<size_t>());
        for (size_t idx : remove_instance_indices) {
            model.scenes.erase(model.scenes.begin() + static_cast<std::ptrdiff_t>(idx));
        }
    }

    model.scenes.insert(model.scenes.end(), extra_instances.begin(), extra_instances.end());
}

} // namespace engine::detail
