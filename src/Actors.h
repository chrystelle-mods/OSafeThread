#pragma once

#include <vector>

// OSafeThread — actor classification for the make-safe buffer.
//
// Given the scene participants, look at the neighborhood (a radius around the scene) and sort
// nearby actors into buckets. The make-safe step then calms the scene actors + the enemies and
// hostiles in that buffer, so a wandering enemy can't detect the scene and trigger combat.

namespace OSafeThread::Actors
{
    struct Classification
    {
        std::vector<RE::Actor*> actors;    // scene participants (input; nulls filtered)
        std::vector<RE::Actor*> enemies;   // nearby, potentially hostile to the player, already in combat
        std::vector<RE::Actor*> allies;    // nearby player teammates (followers)
        std::vector<RE::Actor*> hostiles;  // nearby, potentially hostile to the player, NOT yet in combat
        std::vector<RE::Actor*> neutrals;  // nearby, neither potentially hostile nor a teammate
        RE::NiPoint3            origin{};   // anchor position; {} if none
    };

    // Living, loaded actors within a_radius of a_origin (a ProcessLists scan).
    std::vector<RE::Actor*> GetNearbyActors(const RE::NiPoint3& a_origin, float a_radius);

    // Classify the neighborhood around the scene. Buckets exclude the scene actors and the player.
    Classification Classify(const std::vector<RE::Actor*>& a_sceneActors, float a_radius);

    // Classify the neighborhood around an explicit origin (the pre-thread GetNearby* helpers). Excludes
    // the player and a_exclude (the anchor actor, if any). Buckets are player-relative, like Classify.
    Classification ClassifyAround(const RE::NiPoint3& a_origin, float a_radius, RE::Actor* a_exclude);
}
