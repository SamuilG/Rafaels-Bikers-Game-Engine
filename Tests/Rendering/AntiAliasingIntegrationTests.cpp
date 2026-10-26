#include "Runtime/Renderer/RenderSystem.hpp"
#include "Runtime/UserState/UserState.hpp"
#include "Runtime/Scene/SceneManager.hpp"
#include "Applications/ApplicationPaths.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace engine;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void RunEditorRegression() {
        bool running=true;
        UserState state;
        auto renderState=state.Renderer(); auto sceneState=state.Scene();
        SceneManager scene; scene.SetState(&sceneState); scene.Init();
        RenderSystem renderer(running,&scene);
        renderer.SetState(&renderState); renderer.ConfigureHost(true,true,false);
        renderer.Init();
        // This is a real editor frame loop, with a hidden GLFW window.
        glfwSetWindowSize(renderer.GetGLFWWindow(),640,480); glfwPollEvents();
        ImGui::GetIO().IniFilename="Intermediate/Diagnostics/anti-aliasing/integration-layout.ini";
        state.editor.showRenderSettings=true; state.editor.editorViewportSkybox=true;
        state.editor.showFrameGraph=true;
        state.editor.sceneCamera.RequestFree();
        Require(state.editor.sceneCamera.SetFreeTransform(glm::translate(glm::mat4(1),glm::vec3(0,0,4))),"Cannot position scene camera");
        const auto mesh=generate_uv_sphere(1.f,16,24);
        const uint32_t index=renderer.add_runtime_mesh(mesh);
        auto object=scene.get_world().entity("Anti-aliasing moving sphere")
            .set<LocalTransform>({glm::mat4(1)})
            .set<WorldTransform>({glm::mat4(1)})
            .set<MeshComponent>({index})
            .set<MaterialComponent>({renderer.get_runtime_mat_index()})
            .set<EntityStatus>({true,false});
        uint32_t presented=0;
        auto frame=[&]() {
            glfwPollEvents(); scene.Update(1.f/60); renderer.Update(1.f/60);
            if(renderer.HasPresentedFrame()) ++presented;
            Require(running,"Renderer stopped during anti-aliasing changes");
            const auto& graph = renderer.GetFrameGraph();
            Require(!graph.passes.empty() && graph.passes.back().name == "Present", "Frame graph did not capture the presented path");
            if (!graph.warnings.empty()) for (const auto& warning : graph.warnings) std::cerr << warning << '\n';
            Require(graph.warnings.empty(), "Frame graph found an unwritten render target");
            for (const auto& dep : graph.dependencies) Require(dep.before < dep.after, "Frame graph contains a cycle");
        };
        for(uint32_t samples:{1u,2u,4u,8u,1u}) {
            if(!(state.capabilities.msaaSampleCounts&samples)) continue;
            state.render.msaaSamples=samples;
            for(bool taa:{false,true}) {
                state.render.taaEnabled=taa;
                for(int i=0;i<4;++i) {
                    object.set<LocalTransform>({glm::translate(glm::mat4(1),glm::vec3(i*.03f,0,0))});
                    frame();
                }
                Require(state.capabilities.activeMsaaSamples==samples,"Requested MSAA was not applied");
                const auto& graph = renderer.GetFrameGraph();
                const bool capturedTaa = std::any_of(graph.passes.begin(), graph.passes.end(), [](const auto& pass) { return pass.name == "TAA history resolve"; });
                Require(capturedTaa == taa, "Frame graph TAA path does not match actual settings");
                const bool capturedMsaa = std::any_of(graph.textures.begin(), graph.textures.end(), [](const auto& texture) { return texture.desc.samples > 1 && texture.firstUse != FrameGraph::Invalid; });
                Require(capturedMsaa == (samples > 1), "Frame graph MSAA resources do not match actual settings");
                std::cout<<"PASS editor "<<samples<<"x MSAA, TAA "<<taa<<'\n';
            }
        }
        state.render.msaaSamples=SelectMsaaSamples(4,state.capabilities.msaaSampleCounts);
        state.render.taaEnabled=true;
        // Exercise both geometry and buffer diagnostics with multisampled PBR
        // resources still alive, then return to normal temporal rendering.
        for(int mode:{view_mode::Depth,view_mode::Normals,view_mode::SSAO,view_mode::SSR,view_mode::Default}) {
            editor_viewport::SelectSceneViewMode(state.editor,mode); frame(); frame();
        }
        renderer.SetPortalPreview(glm::translate(glm::mat4(1),glm::vec3(0,0,1.5f)),
            {0,0,4},{0,0,0});
        frame(); frame();
        renderer.SetPortalPreviewPair(glm::translate(glm::mat4(1),glm::vec3(0,0,-1)), {0,0,4}, {0,0,0},
            glm::translate(glm::mat4(1),glm::vec3(0,0,1)) * glm::rotate(glm::mat4(1), glm::radians(180.f), glm::vec3(0,1,0)),
            {0,0,-4}, {0,0,0});
        frame(); frame();
        const auto& portalGraph = renderer.GetFrameGraph();
        const auto portalPasses = std::count_if(portalGraph.passes.begin(), portalGraph.passes.end(), [](const auto& pass) {
            return pass.name == "Portal A forward MRT" || pass.name == "Portal B forward MRT";
        });
        Require(portalPasses > 1, "Linked Portal recursion was not captured");
        Require(std::any_of(portalGraph.textures.begin(), portalGraph.textures.end(), [](const auto& texture) {
            return texture.name.find("scratch color") != std::string::npos && texture.firstUse != FrameGraph::Invalid;
        }), "Portal scratch RT lifetime was not captured");
        renderer.DisablePortalPreview();
        glfwSetWindowSize(renderer.GetGLFWWindow(),800,600); glfwPollEvents(); frame(); frame(); frame();
        Require(state.editor.sceneCamera.SetFreeTransform(glm::translate(glm::mat4(1),glm::vec3(0,0,20))),"Cannot cut scene camera"); frame(); frame();
        state.render.msaaSamples=1; state.render.taaEnabled=false; frame(); frame();
        const auto frozenFrame = renderer.GetFrameGraph().frame;
        state.editor.freezeFrameGraph=true; frame(); frame();
        Require(renderer.GetFrameGraph().frame == frozenFrame, "Frozen frame graph changed");
        glfwSetWindowSize(renderer.GetGLFWWindow(),960,640); glfwPollEvents(); frame(); frame();
        Require(renderer.GetFrameGraph().frame == frozenFrame, "Resize invalidated the frozen graph snapshot");
        state.editor.freezeFrameGraph=false; frame(); frame();
        Require(renderer.GetFrameGraph().frame > frozenFrame, "Frame graph did not resume capture");
        Require(presented>=30,"Too few frames reached actual Vulkan presentation");
        renderer.Shutdown(); scene.Shutdown();
        std::cout<<"PASS diagnostics, portal, resize, camera cut and AA disable; presented="<<presented<<'\n';
}

// Older saved projects contain their own shader copies. They must neither
// hide newly added engine shaders nor override shaders matching the new ABI.
class LegacyProjectResources {
public:
    std::filesystem::path engineRoot = std::filesystem::current_path();
    std::filesystem::path parent = engineRoot / "Intermediate/Diagnostics/anti-aliasing-integration";
    std::filesystem::path root = parent / ("legacy-project-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    LegacyProjectResources() {
        namespace fs = std::filesystem;
        fs::create_directories(root / "Assets/Shaders/spirv");
        auto copy = [&](const fs::path& relative) {
            fs::create_directories((root / relative).parent_path());
            fs::copy_file(engineRoot / relative, root / relative);
        };
        copy("Assets/Skybox/StandardCubeMap.png");
        copy("Assets/Fonts/simhei.ttf");
        for (const char* texture : cfg::ParticleTextures) copy(texture);
        const uint32_t staleShader = 0xdeadbeef;
        for (const char* file : {"Assets/Shaders/spirv/default.vert.spv",
            "Assets/Shaders/spirv/composite.frag.spv", "custom.spv"}) {
            std::ofstream output(root / file, std::ios::binary);
            output.write(reinterpret_cast<const char*>(&staleShader), sizeof(staleShader));
            Require(bool(output), "Cannot create legacy project shader fixture");
        }
    }
    ~LegacyProjectResources() {
        std::error_code ignored;
        std::filesystem::current_path(engineRoot, ignored);
        if (root.parent_path() == parent && root.filename().string().starts_with("legacy-project-"))
            std::filesystem::remove_all(root, ignored);
    }
};

int main(int argc, char** argv) {
    try {
        const auto arguments = engine::apps::CommandLinePaths(argc, argv);
        engine::apps::UseRepositoryAssets(arguments.front());
        RunEditorRegression();
        LegacyProjectResources project;
        // This is the same cwd change performed by Editor when opening a project.
        std::filesystem::current_path(project.root);
        Require(!std::filesystem::exists("Assets/Shaders/spirv/taa_motion.vert.spv"),
            "Legacy fixture unexpectedly contains TAA shaders");
        for (const char* file : {"default.vert.spv", "composite.frag.spv", "taa_motion.vert.spv",
            "taa_motion_skinned.vert.spv", "taa_motion.frag.spv", "taa.frag.spv"}) {
            const auto relative = std::filesystem::path("Assets/Shaders/spirv") / file;
            const auto expected = labut2::load_file_u32((project.engineRoot / relative).string().c_str());
            Require(labut2::load_file_u32(relative.string().c_str()) == expected,
                "Project cwd selected a stale or missing engine shader");
        }
        Require(labut2::load_file_u32("custom.spv") == std::vector<uint32_t>{0xdeadbeef},
            "Explicit shader paths were remapped");
#ifdef _WIN32
        Require(labut2::load_file_u32("Assets/Shaders/spirv\\taa_motion.vert.spv") ==
            labut2::load_file_u32((project.engineRoot / "Assets/Shaders/spirv/taa_motion.vert.spv").string().c_str()),
            "Mixed Windows path separators bypassed the engine shader directory");
#endif
        RunEditorRegression();
        Require(std::filesystem::current_path() == project.root, "Shader loading changed the project asset mount");
        Require(std::filesystem::file_size("Assets/Shaders/spirv/composite.frag.spv") == sizeof(uint32_t),
            "Engine shader loading overwrote the project's shader copy");
        std::cout << "PASS editor reinitialization in an older project: missing TAA and stale shaders, MSAA/TAA toggles\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
