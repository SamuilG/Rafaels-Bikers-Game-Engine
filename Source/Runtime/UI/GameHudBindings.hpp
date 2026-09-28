#pragma once

#include "../Gameplay/HudData.hpp"
#include "VisualUIEditor/UIBinding.hpp"

namespace engine {

// Projection only: publishing bindings never advances gameplay statistics.
inline void BindHudData(UIDataContext& context, const HudData& hud) {
    context.SetFloat("bike.speedKmh", hud.speedKmh);
    context.SetFloat("bike.speedMph", hud.speedMph);
    context.SetFloat("bike.energy", hud.energy);
    context.SetFloat("bike.stamina", hud.stamina);
    context.SetFloat("bike.distance", hud.distanceMeters);
    context.SetBool("bike.isLowEnergy", hud.isLowEnergy);
    context.SetBool("bike.lowStamina", hud.isLowEnergy);
    context.SetColor("bike.warningColor", hud.warningColor);
    context.SetFloat("race.lapTimeSeconds", hud.lapTimeSeconds);
    context.SetFloat("race.bestLapTimeSeconds", hud.bestLapTimeSeconds);
    context.SetInt("race.currentLap", hud.currentLap);
    context.SetString("race.lapTime", hud.lapTime);
    context.SetString("race.bestLapTime", hud.bestLapTime);
}

} // namespace engine
