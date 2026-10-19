#include "Runtime/UI/LightInspector.hpp"
#include <imgui_internal.h>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>

namespace {
struct Item { ImRect bounds, total; ImGuiItemFlags flags; };
std::unordered_map<ImGuiID, Item> items;
void Require(bool ok, const char* message) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
struct Fixture {
    ImGuiContext* context;
    engine::LightComponent light;
    bool primary = true, requested = false;
    Fixture() {
        light.type = engine::LightType::Directional;
        context = ImGui::CreateContext(); context->TestEngineHookItems = true;
        auto& io = ImGui::GetIO(); io.IniFilename = io.LogFilename = nullptr;
        io.DisplaySize = {1200,1500}; io.DeltaTime = 1.0f / 60;
        unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        Frame(); Frame();
    }
    ~Fixture() { ImGui::DestroyContext(context); }
    void Frame() {
        items.clear(); ImGui::NewFrame(); ImGui::SetNextWindowPos({10,10},ImGuiCond_Always);
        ImGui::SetNextWindowSize({300,1400},ImGuiCond_Always); ImGui::Begin("Inspector");
        bool select = false; engine::DrawLightInspector(light,primary,select); requested |= select;
        ImGui::End(); ImGui::Render();
    }
    ImGuiID Id(const char* suffix) { return ImGui::FindWindowByName("Inspector")->GetID((std::string("###Light")+suffix).c_str()); }
    Item Control(const char* suffix) { const auto it=items.find(Id(suffix)); Require(it!=items.end(),"production light control exists"); return it->second; }
    void Click(const char* suffix, float fraction=0.02f) {
        const auto b=Control(suffix).bounds; auto& io=ImGui::GetIO(); io.AddMousePosEvent(b.Min.x+b.GetWidth()*fraction,b.GetCenter().y);
        Frame(); Frame(); io.AddMouseButtonEvent(0,true); Frame(); io.AddMouseButtonEvent(0,false); Frame(); Frame();
    }
    void Number(const char* suffix,const char* value) {
        const auto p=Control(suffix).bounds.GetCenter(); auto& io=ImGui::GetIO(); io.AddMousePosEvent(p.x,p.y); Frame(); Frame();
        io.AddKeyEvent(ImGuiMod_Ctrl,true); io.AddMouseButtonEvent(0,true); Frame(); io.AddMouseButtonEvent(0,false);
        io.AddKeyEvent(ImGuiMod_Ctrl,false); Frame(); io.AddInputCharactersUTF8(value); Frame();
        io.AddKeyEvent(ImGuiKey_Enter,true); Frame(); io.AddKeyEvent(ImGuiKey_Enter,false); Frame();
    }
};
void Controls() {
    Fixture f;
    f.Number("Intensity","3.5"); f.Number("Distance","300"); f.Number("FirstSplit","8"); f.Number("SecondSplit","40");
    f.Number("ReceiverBias","0.001"); f.Click("Strength",.35f); f.Click("FilterRadius",.6f);
    Require(f.light.intensity==3.5f && f.light.shadow.distance==300 && f.light.shadow.firstSplit==8 && f.light.shadow.secondSplit==40,
        "real numeric input edits light intensity and cascade distances");
    Require(std::abs(f.light.shadow.receiverBias-.001f)<.00001f && f.light.shadow.strength<.5f && f.light.shadow.filterRadius>4,
        "real controls edit receiver bias, shadow strength and filtering");
    f.Click("Shadows"); Require(!f.light.castsShadows,"checkbox disables shadow casting");
    const auto oldDistance=f.light.shadow.distance; f.Number("Distance","600");
    Require(f.light.shadow.distance==oldDistance && (f.Control("Distance").flags & ImGuiItemFlags_Disabled),
        "disabled shadow settings reject actual mouse/keyboard edits");
    f.Click("Shadows"); f.primary=false; f.Frame(); f.Click("Primary");
    Require(f.requested && f.light.primaryDirectional,"primary selector submits an explicit entity selection");
    Require(f.light.IsValid(),"edited light remains valid");
    std::puts("PASS real ImGui light/shadow edits, primary selection and disabled controls");
}
void LanguageAndLayout() {
    Fixture f;
    for (auto language : {engine::Language::English,engine::Language::Chinese}) {
        engine::Translator::SetLanguage(language); f.Frame();
        const auto* window=ImGui::FindWindowByName("Inspector");
        for (const char* id : {"Type","Intensity","Specular","Distance","FirstSplit","SecondSplit","Strength","ReceiverBias","FilterRadius","Blend"}) {
            Require(f.Control(id).total.Max.x <= window->WorkRect.Max.x+1,"full control labels fit a 300-pixel inspector");
        }
    }
    f.Number("Intensity","4.5"); Require(f.light.intensity==4.5f,"Chinese controls keep stable IDs and functional input");
    engine::Translator::SetLanguage(engine::Language::English);
    std::puts("PASS bilingual stable IDs and complete labels in the default narrow inspector");
}
}
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*,ImGuiID id,const ImRect& bb,const ImGuiLastItemData* data) {
    if(id) items[id]={data?data->NavRect:bb,bb,data?data->ItemFlags:0};
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID,const char*,ImGuiItemStatusFlags) {}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID) { return nullptr; }
int main() { Controls(); LanguageAndLayout(); }
