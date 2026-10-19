#include "Runtime/Project/ProjectDescriptor.hpp"
#include "Runtime/Core/FrameScheduler.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using engine::ProjectDescriptor;

namespace {
    void Require(bool condition, const std::string& message) {
        if (!condition) throw std::runtime_error(message);
    }

    struct Fixture {
        fs::path base;
        fs::path root;
        fs::path project;
        fs::path descriptor;

        Fixture() {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            base = fs::absolute(fs::path("Intermediate") / "Diagnostics" / "project-descriptor" / "fixtures");
            fs::create_directories(base);
            for (unsigned attempt = 0; attempt != 100; ++attempt) {
                root = base / ("SteerProjectDescriptor-" + std::to_string(stamp) + "-" + std::to_string(attempt));
                if (fs::create_directory(root)) break;
                root.clear();
            }
            Require(!root.empty(), "cannot allocate a unique project fixture");
            project = root / "Projects" / "Bikers";
            descriptor = project / "Bikers.steerproj";
            fs::create_directories(project);
            fs::create_directories(root / "Assets" / "Shaders");
            fs::create_directories(root / "UnrelatedCaller");
        }

        ~Fixture() {
            std::error_code error;
            // Only remove the uniquely created fixture, never its parent.
            if (!root.empty() && root.parent_path() == base &&
                root.filename().string().starts_with("SteerProjectDescriptor-"))
                fs::remove_all(root, error);
        }

        void Write(const std::string& contents) const {
            std::ofstream stream(descriptor);
            Require(static_cast<bool>(stream), "cannot write fixture descriptor");
            stream << contents;
        }
    };

    struct CurrentDirectory {
        fs::path previous = fs::current_path();
        explicit CurrentDirectory(const fs::path& next) { fs::current_path(next); }
        ~CurrentDirectory() { std::error_code error; fs::current_path(previous, error); }
    };

    std::string Manifest() {
        return R"({"schemaVersion":1,"name":"Rafael's Bikers","contentModule":"bikers","startupScene":"level1","assetRoot":"../../Assets","sceneSnapshot":"Scenes/Main.snapshot.json"})";
    }

    std::string Replace(std::string json, const std::string& before, const std::string& after) {
        const auto offset = json.find(before);
        Require(offset != std::string::npos, "invalid test JSON replacement");
        json.replace(offset, before.size(), after);
        return json;
    }

    void Reject(const Fixture& fixture, const std::string& contents, const char* message) {
        fixture.Write(contents);
        try { (void)ProjectDescriptor::Load(fixture.descriptor); }
        catch (const std::runtime_error&) { return; }
        throw std::runtime_error(std::string("accepted invalid project: ") + message);
    }

    void TestDescriptorRelativePaths() {
        Fixture fixture;
        fixture.Write(Manifest());
        CurrentDirectory caller(fixture.root / "UnrelatedCaller");
        const auto loaded = ProjectDescriptor::Load(fixture.descriptor);
        Require(loaded.name == "Rafael's Bikers" && loaded.contentModule == "bikers" && loaded.startupScene == "level1",
            "descriptor content is preserved");
        Require(loaded.filePath.is_absolute() && loaded.assetRoot.is_absolute() && loaded.snapshotPath.is_absolute(),
            "all resolved descriptor paths are absolute");
        Require(fs::equivalent(loaded.filePath, fixture.descriptor), "descriptor resolves to the actual input file");
        Require(fs::equivalent(loaded.assetRoot, fixture.root / "Assets"), "assetRoot is relative to the descriptor, not cwd");
        Require(loaded.snapshotPath.lexically_normal() == (fixture.project / "Scenes" / "Main.snapshot.json").lexically_normal(),
            "snapshot is relative to the descriptor, not cwd");
        const auto relativeFile = fs::relative(fixture.descriptor, fs::current_path());
        Require(fs::equivalent(ProjectDescriptor::Load(relativeFile).filePath, fixture.descriptor),
            "a relative descriptor argument is accepted and made absolute");
        std::puts("PASS descriptor paths resolve against the manifest independently of process cwd");
    }

    void TestPackagedAssetsAndDefaultSnapshot() {
        Fixture fixture;
        fs::create_directories(fixture.project / "Assets" / "Shaders");
        auto json = Replace(Manifest(), "../../Assets", "Assets");
        json = Replace(json, ",\"sceneSnapshot\":\"Scenes/Main.snapshot.json\"", "");
        fixture.Write(json);
        const auto loaded = ProjectDescriptor::Load(fixture.descriptor);
        Require(fs::equivalent(loaded.assetRoot, fixture.project / "Assets"), "portable package uses adjacent Assets directory");
        Require(loaded.snapshotPath == fixture.project / "Scenes" / "Main.snapshot.json", "omitted snapshot selects the project default");
        Require(!fs::exists(loaded.snapshotPath), "an unsaved project does not require a snapshot file");
        fs::create_directories(loaded.snapshotPath.parent_path());
        std::ofstream(loaded.snapshotPath) << "{}";
        Require(ProjectDescriptor::Load(fixture.descriptor).snapshotPath == loaded.snapshotPath, "an existing snapshot is accepted");
        std::puts("PASS packaged Assets paths, default snapshot and absent or existing snapshots");
    }

    void TestSchemaAndMalformedInput() {
        Fixture fixture;
        Reject(fixture, Replace(Manifest(), "\"schemaVersion\":1", "\"schemaVersion\":2"), "unknown schema");
        Reject(fixture, Replace(Manifest(), "\"schemaVersion\":1", "\"schemaVersion\":\"1\""), "string schema");
        Reject(fixture, Replace(Manifest(), "\"schemaVersion\":1,", ""), "missing schema");
        Reject(fixture, "{ broken JSON", "malformed JSON");
        Reject(fixture, "[]", "non-object JSON");
        bool missingRejected = false;
        try { (void)ProjectDescriptor::Load(fixture.project / "Missing.steerproj"); }
        catch (const std::runtime_error&) { missingRejected = true; }
        Require(missingRejected, "missing descriptor must throw runtime_error");
        std::puts("PASS unsupported or invalid schema, malformed JSON and missing descriptor are rejected");
    }

    void TestRequiredStrings() {
        Fixture fixture;
        for (const auto& field : {std::string("\"name\":\"Rafael's Bikers\""),
            std::string("\"contentModule\":\"bikers\""), std::string("\"startupScene\":\"level1\"")}) {
            const auto colon = field.find(':');
            const auto key = field.substr(0, colon + 1);
            Reject(fixture, Replace(Manifest(), field, key + "\"\""), "empty required string");
            Reject(fixture, Replace(Manifest(), field, key + "17"), "numeric required string");
            Reject(fixture, Replace(Manifest(), field + ",", ""), "missing required string");
        }
        std::puts("PASS project name, content module and startup scene require nonempty strings");
    }

    void TestAssetValidation() {
        Fixture fixture;
        Reject(fixture, Replace(Manifest(), "../../Assets", "Missing/Assets"), "missing Assets directory");
        fs::create_directories(fixture.project / "NoShaders" / "Assets");
        Reject(fixture, Replace(Manifest(), "../../Assets", "NoShaders/Assets"), "Assets without Shaders");
        fs::create_directories(fixture.project / "Textures" / "Shaders");
        Reject(fixture, Replace(Manifest(), "../../Assets", "Textures"), "asset directory with an incompatible name");
        std::ofstream(fixture.project / "Assets") << "not a directory";
        Reject(fixture, Replace(Manifest(), "../../Assets", "Assets"), "Assets is a file");
        Reject(fixture, Replace(Manifest(), "../../Assets", (fixture.root / "Assets").generic_string()), "absolute Assets path");
        Reject(fixture, Replace(Manifest(), "\"assetRoot\":\"../../Assets\"", "\"assetRoot\":false"), "non-string Assets path");
        std::puts("PASS asset roots require a relative existing Assets directory with Shaders");
    }

    void TestSnapshotContainment() {
        Fixture fixture;
        Reject(fixture, Replace(Manifest(), "Scenes/Main.snapshot.json", "../Outside.snapshot.json"), "parent traversal");
        Reject(fixture, Replace(Manifest(), "Scenes/Main.snapshot.json", "../BikersSibling/Main.snapshot.json"), "same-prefix sibling escape");
        Reject(fixture, Replace(Manifest(), "Scenes/Main.snapshot.json", (fixture.root / "Outside.snapshot.json").generic_string()), "absolute snapshot");
        Reject(fixture, Replace(Manifest(), "Scenes/Main.snapshot.json", "Scenes/../../Outside.snapshot.json"), "nested parent traversal");
        fixture.Write(Replace(Manifest(), "Scenes/Main.snapshot.json", "Scenes/Sub/../Main.snapshot.json"));
        const auto loaded = ProjectDescriptor::Load(fixture.descriptor);
        Require(loaded.snapshotPath.lexically_normal() == (fixture.project / "Scenes" / "Main.snapshot.json").lexically_normal(),
            "normalizable nested snapshot stays inside the project");
        std::puts("PASS snapshot absolute paths and project escapes are rejected without blocking safe normalization");
    }

    void TestSnapshotFieldType() {
        Fixture fixture;
        Reject(fixture, Replace(Manifest(), "\"sceneSnapshot\":\"Scenes/Main.snapshot.json\"", "\"sceneSnapshot\":37"), "non-string snapshot");
        Reject(fixture, Replace(Manifest(), "Scenes/Main.snapshot.json", ""), "empty snapshot path");
        std::puts("PASS explicitly supplied snapshot paths require a nonempty string");
    }

    void TestContentRegistryIsHostOwned() {
        Fixture fixture;
        fixture.Write(Replace(Manifest(), "\"contentModule\":\"bikers\"", "\"contentModule\":\"another-game\""));
        const auto loaded = ProjectDescriptor::Load(fixture.descriptor);
        Require(loaded.contentModule == "another-game", "descriptor loader preserves module names for host-side factory validation");
        std::puts("PASS descriptor loader leaves content-module support to the host factory");
    }

    void TestEditingProjectFrameGate() {
        engine::GameFlowController flow;
        Require(flow.Request(engine::GameFlowCommand::Start), "queue project simulation start");
        const auto reload = []() -> bool { throw std::runtime_error("unexpected project reload"); };
        const auto editing = engine::FrameScheduler::BeginFrame(flow, reload, true);
        Require(flow.State() == engine::GameFlowState::Playing && editing.flowState == engine::GameFlowState::Playing,
            "editing mode freezes a committed Playing state without changing game flow");
        Require(!editing.runGameplay && !editing.runSimulationSystems && editing.runPresentation,
            "editing mode freezes gameplay and world simulation while presenting the scene");
        std::vector<std::string> stages;
        engine::FrameSteps steps;
        steps.gameplay = [&](float) { stages.emplace_back("gameplay"); };
        steps.events = [&](float) { stages.emplace_back("events"); };
        steps.physics = [&](float) { stages.emplace_back("physics"); };
        steps.postPhysics = [&](float) { stages.emplace_back("postPhysics"); };
        steps.scene = [&](float) { stages.emplace_back("scene"); };
        steps.animation = [&](float) { stages.emplace_back("animation"); };
        steps.gameEffects = [&](float) { stages.emplace_back("gameEffects"); };
        steps.presentation = [&](float) { stages.emplace_back("presentation"); };
        steps.audio = [&](float) { stages.emplace_back("audio"); };
        engine::FrameScheduler::Execute(editing, 0.016f, steps);
        Require(stages == std::vector<std::string>{"scene", "presentation", "audio"},
            "editing a project keeps only scene, presentation and audio stages active");
        const auto playing = engine::FrameScheduler::BeginFrame(flow, reload, false);
        Require(playing.runGameplay && playing.runSimulationSystems && playing.runPresentation,
            "leaving project editing resumes committed gameplay");
        stages.clear();
        engine::FrameScheduler::Execute(playing, 0.016f, steps);
        Require(stages == std::vector<std::string>{"gameplay", "events", "physics", "postPhysics",
            "scene", "animation", "gameEffects", "presentation", "audio"},
            "normal play executes the full production simulation pipeline");
        std::puts("PASS project editing gates the unified frame while scene, presentation and audio remain active");
    }

    void TestExistingSnapshotPreflight() {
        Fixture fixture;
        fixture.Write(Manifest());
        const auto snapshot = fixture.project / "Scenes" / "Main.snapshot.json";
        fs::create_directories(snapshot.parent_path());
        const auto rejectSnapshot = [&](const char* contents) {
            std::ofstream(snapshot) << contents;
            try { (void)ProjectDescriptor::Load(fixture.descriptor); }
            catch (const std::runtime_error& error) {
                const std::string message = error.what();
                Require(message.find(snapshot.string()) != std::string::npos ||
                    message.find(snapshot.generic_string()) != std::string::npos,
                    "snapshot validation error identifies its complete file path");
                return;
            }
            throw std::runtime_error("accepted a malformed existing snapshot before scene mutation");
        };
        rejectSnapshot("{ broken snapshot JSON");
        rejectSnapshot(R"({"Entities":[{"Name":"Bike_0","Position":[0,0],"Rotation":[0,0,0],"Scale":[1,1,1]}]})");
        rejectSnapshot(R"({"Entities":[{"Name":"Bike_0","Position":[0,"bad",0],"Rotation":[0,0,0],"Scale":[1,1,1]}]})");
        rejectSnapshot(R"({"Entities":[{"Name":"Bike_0","Position":[0,0,0],"Rotation":"0,0,0","Scale":[1,1,1]}]})");
        rejectSnapshot(R"({"Particles":[{"MaxParticles":4294967296}]})");
        rejectSnapshot(R"({"Entities":[{"Name":"PlayerCamera","Position":[0,2,5],"Rotation":[0,0,0],"Scale":[1,1,1],"Camera":{"Mode":"Portal"}}]})");
        rejectSnapshot(R"({"Entities":[{"Name":"PlayerCamera","Position":[0,2,5],"Rotation":[0,0,0],"Scale":[1,1,1],"Camera":{"Mode":"Free","NearPlane":5,"FarPlane":1}}]})");
        rejectSnapshot(R"({"Entities":[{"Name":"PlayerCamera","Position":[0,2,5],"Rotation":[0,0,0],"Scale":[2,1,1],"Camera":{"Mode":"Free"}}]})");
        for (const auto* contents : {
            "{}",
            R"({"Entities":[{"Name":"PlayerCamera","Position":[0,2,5],"Rotation":[0,0,0],"Scale":[1,1,1],"Camera":{"Mode":"Free","AutomaticFov":false,"Fov":75}}]})",
            R"({"Entities":[{"Name":"Bike_0","Position":[0,1,2],"Rotation":[0,0,0],"Scale":[1,1,1]}]})",
            R"({"Particles":[{}, {"Name":"Legacy emitter"}]})"}) {
            std::ofstream(snapshot) << contents;
            Require(ProjectDescriptor::Load(fixture.descriptor).snapshotPath == snapshot,
                "legacy snapshots may omit entity/particle lists and optional particle properties");
        }
        std::puts("PASS existing snapshots are preflighted with file-path errors and legacy optional fields preserved");
    }
}

int main() try {
    TestDescriptorRelativePaths();
    TestPackagedAssetsAndDefaultSnapshot();
    TestSchemaAndMalformedInput();
    TestRequiredStrings();
    TestAssetValidation();
    TestSnapshotContainment();
    TestSnapshotFieldType();
    TestContentRegistryIsHostOwned();
    TestEditingProjectFrameGate();
    TestExistingSnapshotPreflight();
    std::puts("All 10 project descriptor/frame regression groups passed (real temporary files, no GPU).");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
