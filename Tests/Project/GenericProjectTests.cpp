#include "Runtime/Project/ProjectStorage.hpp"
#include <chrono>
#include <cstdio>
#include <limits>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace engine;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> static void Reject(F action, const char* message) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
static Json Identity() { return Json::array({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}); }
static Json Entity(const char* id, const char* name) {
    return {{"id",id},{"name",name},{"parent",nullptr},{"transform",Identity()},
        {"status",{{"visible",true},{"physics",false}}},{"layer",0}};
}
static Json Document() {
    auto camera = Entity("camera", "PlayerCamera");
    CameraSettings settings;
    settings.mode = CameraMode::Free;
    settings.automaticFov = false;
    settings.fov = 63.0f;
    camera["camera"] = {{"settings", SerializeCameraSettings(settings)}, {"freeTransform", Identity()}};
    return {{"schemaVersion",1},{"models",Json::array()},
        {"entities",Json::array({camera, Entity("parent", "Parent"), Entity("child", "Child")})},
        {"particles",Json::array()}};
}
static void Write(const fs::path& file, const std::string& value) {
    fs::create_directories(file.parent_path());
    std::ofstream stream(file, std::ios::binary);
    stream << value;
    Require(static_cast<bool>(stream), "fixture file write failed");
}
static std::string Read(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    Require(static_cast<bool>(stream), "fixture file read failed");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
struct Fixture {
    fs::path base = fs::absolute("Intermediate/Diagnostics/generic-project/fixtures");
    fs::path root;
    Fixture() {
        fs::create_directories(base);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            const auto candidate = base / ("project-" + std::to_string(stamp) + "-" + std::to_string(i));
            if (fs::create_directory(candidate)) { root = candidate; break; }
        }
        Require(!root.empty(), "cannot allocate fixture");
        Write(root / "source/Assets/Shaders/example.spv", "shader fixture");
        Write(root / "source/Assets/Models/example.glb", "model fixture for filesystem preflight only");
        fs::create_directories(root / "saved");
    }
    ~Fixture() {
        std::error_code ignored;
        if (!root.empty() && root.parent_path() == base && root.filename().string().starts_with("project-"))
            fs::remove_all(root, ignored);
    }
};

static void ValidateDocuments() {
    auto valid = Document();
    valid["entities"][2]["parent"] = "parent";
    ValidateSceneDocument(valid);
    const auto bad = [&](auto mutate, const char* message) {
        auto value = valid; mutate(value);
        Reject([&] { ValidateSceneDocument(value); }, message);
    };
    bad([](Json& j) { j["entities"][2]["id"]="parent"; }, "accepted duplicate identity");
    bad([](Json& j) { j["entities"][1]["parent"]="child"; }, "accepted parent cycle");
    bad([](Json& j) { j["entities"][2]["parent"]="absent"; }, "accepted missing parent");
    bad([](Json& j) { j["entities"][1]["transform"][0]=std::numeric_limits<double>::infinity(); }, "accepted non-finite matrix");
    bad([](Json& j) { j["entities"][0]["camera"]["freeTransform"][0]=2; }, "accepted scaled camera");
    bad([](Json& j) { j["entities"][2]["parent"]=nullptr; j["entities"][2]["name"]="Parent"; }, "accepted duplicate sibling name");
    bad([](Json& j) { j["entities"][2]["model"]={{"instance","missing"},{"part",0}}; }, "accepted missing model import");
    auto imported = valid;
    imported["models"].push_back({{"id","import1"},{"asset","Assets/Models/example.glb"},
        {"physicsType",0},{"mass",0},{"transform",Identity()}});
    imported["entities"][2]["model"]={{"instance","import1"},{"part",0}};
    imported["entities"][2]["status"]["physics"]=true;
    ValidateSceneDocument(imported);
    for (const char* path : {"../escape.glb", "Assets/../escape.glb", "C:/escape.glb", "Assets\\model.glb"}) {
        auto invalid = imported; invalid["models"][0]["asset"]=path;
        Reject([&] { ValidateSceneDocument(invalid); }, "accepted escaping model path");
    }
    auto duplicate = imported;
    duplicate["entities"].push_back(Entity("duplicate", "Duplicate"));
    duplicate["entities"].back()["model"]={{"instance","import1"},{"part",0}};
    Reject([&] { ValidateSceneDocument(duplicate); }, "accepted duplicate model part");
    std::puts("PASS scene schema: identity, hierarchy, matrix, camera and resource-reference validation");
}

static void SaveRoundTrip() {
    Fixture fixture;
    const auto source = fixture.root / "source/Assets";
    const auto requested = fixture.root / fs::u8path("saved/测试工程.steerproj");
    auto scene = Document();
    scene["entities"][2]["parent"]="parent";
    const auto originalCwd = fs::current_path();
    const auto saved = project_storage::Save(requested, source, scene);
    Require(fs::current_path()==originalCwd, "save changed process resource root");
    Require(saved.name=="测试工程" && saved.contentModule=="scene", "Unicode project identity lost");
    Require(Json::parse(Read(saved.snapshotPath))==scene, "scene did not round-trip through storage");
    Require(Read(saved.assetRoot/"Models/example.glb")==Read(source/"Models/example.glb"), "resource not packaged");
    Require(fs::is_regular_file(saved.assetRoot/"Shaders/example.spv"), "shader resource not packaged");
    scene["entities"][1]["transform"][12]=42;
    Write(source/"Models/later.glb", "new resource");
    project_storage::Save(requested, source, scene);
    Require(Json::parse(Read(saved.snapshotPath))==scene, "ordinary Save lost changed transform");
    Require(Read(saved.assetRoot/"Models/later.glb")=="new resource", "ordinary Save missed new resource");
    project_storage::Save(requested, saved.assetRoot, scene);
    Require(Json::parse(Read(saved.snapshotPath))==scene, "saving an opened packaged project failed");
    const auto destination=fixture.root/"moved";
    fs::rename(fixture.root/"saved",destination);
    const auto moved=ProjectDescriptor::Load(destination/requested.filename());
    Require(Json::parse(Read(moved.snapshotPath))==scene && fs::exists(moved.assetRoot/"Models/later.glb"),
        "portable project references did not survive relocation");
    std::puts("PASS Unicode Save As, repeated Save, new assets, own-root Save and relocated reopen");
}

static void ResourcePreflight() {
    Fixture fixture;
    const auto assets=fixture.root/"source/Assets";
    auto document=Document();
    document["models"].push_back({{"id","asset1"},{"asset","Assets/Models/example.glb"},
        {"physicsType",0},{"mass",0},{"transform",Identity()}});
    document["entities"][2]["model"]={{"instance","asset1"},{"part",0}};
    document["entities"][2]["status"]["physics"]=true;
    Reject([&] { ValidateSceneDocumentAssets(document,assets); }, "accepted malformed GLB header");
    const auto glb=[&](const char* uri) {
        std::string chunk=Json({{"asset",{{"version","2.0"}}},{"buffers",Json::array({{{"uri",uri},{"byteLength",4}}})}}).dump();
        while (chunk.size()%4) chunk.push_back(' ');
        std::ofstream stream(assets/"Models/example.glb",std::ios::binary|std::ios::trunc);
        const std::uint32_t header[]={0x46546c67,2,static_cast<std::uint32_t>(20+chunk.size()),
            static_cast<std::uint32_t>(chunk.size()),0x4e4f534a};
        stream.write(reinterpret_cast<const char*>(header),sizeof(header));
        stream.write(chunk.data(),static_cast<std::streamsize>(chunk.size()));
    };
    Write(assets/"Models/mesh.bin","data");
    glb("mesh.bin");
    ValidateSceneDocumentAssets(document,assets);
    const auto repairPath=fixture.root/"saved/Repair.steerproj";
    const auto repairProject=project_storage::Save(repairPath,assets,document);
    fs::remove(repairProject.assetRoot/"Models/example.glb");
    auto repaired=document;
    repaired["models"]=Json::array();
    repaired["entities"].erase(2);
    project_storage::Save(repairPath,repairProject.assetRoot,repaired);
    Require(Json::parse(Read(ProjectDescriptor::Load(repairPath).snapshotPath))==repaired,
        "Save could not repair an old document after deleting a referenced asset and its entities");
    glb("missing.bin");
    Reject([&] { ValidateSceneDocumentAssets(document,assets); }, "accepted missing GLB buffer");
    Write(fixture.root/"source/outside.bin","data");
    glb("../../outside.bin");
    Reject([&] { ValidateSceneDocumentAssets(document,assets); }, "accepted external dependency outside Assets");
    glb("%2e%2e/%2e%2e/outside.bin");
    Reject([&] { ValidateSceneDocumentAssets(document,assets); }, "accepted encoded external dependency escape");
    auto shear=document;
    shear["entities"][2]["transform"][4]=0.5;
    Reject([&] { ValidateSceneDocument(shear); }, "accepted physical shear with mismatched collision shape");
    std::puts("PASS GLB dependencies, removal repair, encoded traversal and physical-shear rejection");
}

static void FailurePreservesFiles() {
    Fixture fixture;
    const auto source=fixture.root/"source/Assets";
    const auto path=fixture.root/"saved/World.steerproj";
    const auto saved=project_storage::Save(path,source,Document());
    const auto oldManifest=Read(path), oldScene=Read(saved.snapshotPath);
    auto invalid=Document(); invalid["entities"][2]["parent"]="missing";
    Reject([&] { project_storage::Save(path,source,invalid); }, "saved invalid scene");
    Require(Read(path)==oldManifest && Read(saved.snapshotPath)==oldScene, "invalid save changed existing files");
    Write(fixture.root/"saved/Occupied.steer/keep.txt","unrelated content");
    Reject([&] { project_storage::Save(fixture.root/"saved/Occupied.steerproj",source,Document()); }, "overwrote unowned data directory");
    Require(Read(fixture.root/"saved/Occupied.steer/keep.txt")=="unrelated content", "unowned content changed");
    Reject([&] { project_storage::Save(source/"recursive.steerproj",source,Document()); }, "accepted recursive asset destination");
    Reject([&] { project_storage::Save(fixture.root/"saved/Wrong.txt",source,Document()); }, "accepted wrong extension");
    // Force a commit-time failure after an asset replacement has succeeded.
    Write(source/"Models/example.glb","changed asset content of a different length");
    const auto originalAsset=Read(saved.assetRoot/"Models/example.glb");
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(saved.snapshotPath.c_str());
    Require(SetFileAttributesW(saved.snapshotPath.c_str(), attributes|FILE_ATTRIBUTE_READONLY)!=0,
        "cannot create read-only scene fixture");
    struct RestoreAttributes {
        fs::path path; DWORD attributes;
        ~RestoreAttributes() { SetFileAttributesW(path.c_str(), attributes); }
    } restore{saved.snapshotPath,attributes};
    Reject([&] { project_storage::Save(path,source,Document()); }, "overwrote read-only scene");
    Require(Read(saved.assetRoot/"Models/example.glb")==originalAsset && Read(path)==oldManifest,
        "failed save altered old project or model");
    Require(Read(saved.snapshotPath)==oldScene,"failed save lost original scene");
#endif
    std::puts("PASS failed saves preserve existing project/assets and refuse unrelated destinations");
}

int main() try {
    ValidateDocuments(); SaveRoundTrip(); ResourcePreflight(); FailurePreservesFiles();
    std::puts("All generic project storage and schema tests passed.");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
}
