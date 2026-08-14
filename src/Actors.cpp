#include "Actors.h"

#include <unordered_set>

namespace
{
    bool WithinRadius(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b, float a_r2)
    {
        const float dx = a_a.x - a_b.x;
        const float dy = a_a.y - a_b.y;
        const float dz = a_a.z - a_b.z;
        return (dx * dx + dy * dy + dz * dz) <= a_r2;
    }

    // Sort one accepted nearby actor (already filtered: non-null, not the player, not excluded) into a
    // classification bucket. "Potentially hostile to the player" is a UNION of disposition signals,
    // because no single one is sufficient (verified in-game via DevBench, 2026-08-14):
    //   * IsHostileToActor        — the game's composite check; catches most, incl. undetected
    //                               faction-enemies (an undetected bear read True here).
    //   * FactionReaction==Enemy   — explicit faction enemies (bears/predators read Enemy).
    //   * aggression>=VeryAggressive & not ally/friend — aggression-driven attackers like bandits,
    //                               whose faction reaction is Neutral, so nothing above would catch an
    //                               UNDETECTED one. Over-inclusion is harmless (a calmed neutral is
    //                               unaffected); a missed hostile risks the very crash we prevent.
    void BucketActor(OSafeThread::Actors::Classification& a_c, RE::Actor* a_actor, RE::PlayerCharacter* a_player)
    {
        if (a_actor->IsPlayerTeammate()) {
            a_c.allies.push_back(a_actor);
            return;
        }

        bool potentiallyHostile = false;
        if (a_player) {
            const auto reaction       = a_actor->GetFactionReaction(a_player);
            const auto base           = a_actor->GetActorBase();
            const bool veryAggressive = base && base->GetAggressionLevel() >= RE::ACTOR_AGGRESSION::kVeryAggressive;
            potentiallyHostile =
                a_actor->IsHostileToActor(a_player) ||
                reaction == RE::FIGHT_REACTION::kEnemy ||
                (veryAggressive && reaction != RE::FIGHT_REACTION::kAlly && reaction != RE::FIGHT_REACTION::kFriend);
        }

        if (potentiallyHostile) {
            if (a_actor->IsInCombat()) {
                a_c.enemies.push_back(a_actor);
            } else {
                a_c.hostiles.push_back(a_actor);
            }
        } else {
            a_c.neutrals.push_back(a_actor);
        }
    }
}

std::vector<RE::Actor*> OSafeThread::Actors::GetNearbyActors(const RE::NiPoint3& a_origin, float a_radius)
{
    std::vector<RE::Actor*> result;
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        return result;
    }
    const float r2 = a_radius * a_radius;

    const auto scan = [&](RE::BSTArray<RE::ActorHandle>& a_arr) {
        for (auto& handle : a_arr) {
            auto      ptr = handle.get();
            RE::Actor* actor = ptr.get();
            if (!actor || actor->IsDisabled() || actor->IsDead() || !actor->Is3DLoaded()) {
                continue;
            }
            if (WithinRadius(actor->GetPosition(), a_origin, r2)) {
                result.push_back(actor);
            }
        }
    };

    scan(processLists->highActorHandles);
    scan(processLists->middleHighActorHandles);
    scan(processLists->middleLowActorHandles);
    scan(processLists->lowActorHandles);
    return result;
}

OSafeThread::Actors::Classification OSafeThread::Actors::Classify(const std::vector<RE::Actor*>& a_sceneActors,
                                                                  float a_radius)
{
    Classification c;
    auto*          player = RE::PlayerCharacter::GetSingleton();

    std::unordered_set<RE::FormID> sceneSet;
    RE::NiPoint3                   origin{};
    bool                          haveOrigin = false;
    for (auto* actor : a_sceneActors) {
        if (actor) {
            c.actors.push_back(actor);
            sceneSet.insert(actor->GetFormID());
            if (!haveOrigin) {
                origin     = actor->GetPosition();
                haveOrigin = true;
            }
        }
    }
    if (!haveOrigin) {
        return c;  // no valid scene actor to anchor the buffer on
    }
    c.origin = origin;  // stash the anchor so callers can distance-filter the buffer later

    for (auto* actor : GetNearbyActors(origin, a_radius)) {
        if (!actor || actor->IsPlayerRef() || sceneSet.contains(actor->GetFormID())) {
            continue;
        }
        BucketActor(c, actor, player);
    }
    return c;
}

OSafeThread::Actors::Classification OSafeThread::Actors::ClassifyAround(const RE::NiPoint3& a_origin,
                                                                       float a_radius, RE::Actor* a_exclude)
{
    Classification   c;
    auto*            player    = RE::PlayerCharacter::GetSingleton();
    const RE::FormID excludeId = a_exclude ? a_exclude->GetFormID() : 0;
    c.origin                   = a_origin;

    for (auto* actor : GetNearbyActors(a_origin, a_radius)) {
        if (!actor || actor->IsPlayerRef() || (excludeId != 0 && actor->GetFormID() == excludeId)) {
            continue;
        }
        BucketActor(c, actor, player);
    }
    return c;
}
