#include "Papyrus.h"

#include "Actors.h"
#include "Calm.h"
#include "Config.h"
#include "SceneManager.h"
#include "ThreadEventSink.h"

namespace
{
    // Must equal the .psc Scriptname (data/Source/Scripts/OSafeThread.psc).
    constexpr std::string_view kClass = "OSafeThread"sv;

    // Presence / version probe. Integrating mods call OSafeThread.GetVersion() to detect OSafeThread.
    // NB: named GetPluginVersion, not GetVersion — the latter collides with the Win32
    // ::GetVersion() from <Windows.h>, making the name an overload set that breaks template
    // deduction in RegisterFunction. The Papyrus-facing name below is still "GetVersion".
    RE::BSFixedString GetPluginVersion(RE::StaticFunctionTag*)
    {
        return "1.0.0"sv;
    }

    // Phase-0 smoke test: returns the threadId of the most recent OStim scene OSafeThread observed
    // starting (-1 if none yet). Proves event capture + native binding end to end.
    std::int32_t GetLastStartedThreadId(RE::StaticFunctionTag*)
    {
        return OSafeThread::ThreadEventSink::LastStartedThreadId();
    }

    // --- Calm-engine debug functions (test the detection/combat suppression in isolation) ---
    void CalmActor(RE::StaticFunctionTag*, RE::Actor* a_actor)
    {
        if (a_actor) {
            OSafeThread::Calm::Add(a_actor->GetFormID());
        }
    }

    void UncalmActor(RE::StaticFunctionTag*, RE::Actor* a_actor)
    {
        if (a_actor) {
            OSafeThread::Calm::Remove(a_actor->GetFormID());
        }
    }

    bool IsActorCalmed(RE::StaticFunctionTag*, RE::Actor* a_actor)
    {
        return a_actor && OSafeThread::Calm::Contains(a_actor->GetFormID());
    }

    // --- Make-safe scene tracking (driven by the Start Papyrus wrapper) ---
    void PrepareThread(RE::StaticFunctionTag*, std::vector<RE::Actor*> a_actors, RE::BSFixedString a_owner)
    {
        OSafeThread::SceneManager::GetSingleton()->PrepareThread(a_actors, std::string{ a_owner.c_str() });
    }

    void BindThread(RE::StaticFunctionTag*, std::int32_t a_threadId)
    {
        OSafeThread::SceneManager::GetSingleton()->BindThread(a_threadId);
    }

    std::vector<RE::Actor*> GetSceneActors(RE::StaticFunctionTag*, std::int32_t a_threadId)
    {
        return OSafeThread::SceneManager::GetSingleton()->GetActors(a_threadId);
    }

    std::vector<RE::Actor*> GetEnemies(RE::StaticFunctionTag*, std::int32_t a_threadId)
    {
        return OSafeThread::SceneManager::GetSingleton()->GetEnemies(a_threadId);
    }

    std::vector<RE::Actor*> GetAllies(RE::StaticFunctionTag*, std::int32_t a_threadId)
    {
        return OSafeThread::SceneManager::GetSingleton()->GetAllies(a_threadId);
    }

    std::vector<RE::Actor*> GetHostiles(RE::StaticFunctionTag*, std::int32_t a_threadId)
    {
        return OSafeThread::SceneManager::GetSingleton()->GetHostiles(a_threadId);
    }

    std::vector<RE::Actor*> GetNeutrals(RE::StaticFunctionTag*, std::int32_t a_threadId)
    {
        return OSafeThread::SceneManager::GetSingleton()->GetNeutrals(a_threadId);
    }

    // --- Pre-thread neighborhood classification (the GetNearby* helpers) ---------------------------
    // Classify actors around akCenter (None => the player) within afRadius (0.0 => the config buffer).
    // Buckets are player-relative and use the same definitions as the in-thread readers above.
    RE::NiPoint3 NearbyOrigin(RE::TESObjectREFR* a_center)
    {
        if (a_center) {
            return a_center->GetPosition();
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        return player ? player->GetPosition() : RE::NiPoint3{};
    }

    OSafeThread::Actors::Classification ClassifyNear(RE::TESObjectREFR* a_center, float a_radius)
    {
        const float radius = a_radius > 0.0f ? a_radius : OSafeThread::Config::GetSingleton()->bufferRadius;
        auto*       exclude = a_center ? a_center->As<RE::Actor>() : nullptr;
        return OSafeThread::Actors::ClassifyAround(NearbyOrigin(a_center), radius, exclude);
    }

    std::vector<RE::Actor*> GetNearbyEnemies(RE::StaticFunctionTag*, RE::TESObjectREFR* a_center, float a_radius)
    {
        return ClassifyNear(a_center, a_radius).enemies;
    }

    std::vector<RE::Actor*> GetNearbyAllies(RE::StaticFunctionTag*, RE::TESObjectREFR* a_center, float a_radius)
    {
        return ClassifyNear(a_center, a_radius).allies;
    }

    std::vector<RE::Actor*> GetNearbyHostiles(RE::StaticFunctionTag*, RE::TESObjectREFR* a_center, float a_radius)
    {
        return ClassifyNear(a_center, a_radius).hostiles;
    }

    std::vector<RE::Actor*> GetNearbyNeutrals(RE::StaticFunctionTag*, RE::TESObjectREFR* a_center, float a_radius)
    {
        return ClassifyNear(a_center, a_radius).neutrals;
    }

    // The whole classified buffer (every nearby actor except the player and the center), unsorted.
    std::vector<RE::Actor*> GetNearbyBuffer(RE::StaticFunctionTag*, RE::TESObjectREFR* a_center, float a_radius)
    {
        const auto              c = ClassifyNear(a_center, a_radius);
        std::vector<RE::Actor*> all;
        all.reserve(c.enemies.size() + c.hostiles.size() + c.allies.size() + c.neutrals.size());
        all.insert(all.end(), c.enemies.begin(), c.enemies.end());
        all.insert(all.end(), c.hostiles.begin(), c.hostiles.end());
        all.insert(all.end(), c.allies.begin(), c.allies.end());
        all.insert(all.end(), c.neutrals.begin(), c.neutrals.end());
        return all;
    }

    // Currently-calmed actors, optionally limited to those within afMaxDistance units of the scene
    // (<= 0 => all). Use this to find the near calmed bystanders a mod may want to Claim.
    std::vector<RE::Actor*> GetCalmedActors(RE::StaticFunctionTag*, std::int32_t a_threadId, float a_maxDistance)
    {
        return OSafeThread::SceneManager::GetSingleton()->GetCalmedActors(a_threadId, a_maxDistance);
    }

    // --- Ownership + claim/release (writes are gated on the owner id matching the scene's owner) ---
    bool ClaimActor(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::BSFixedString a_owner)
    {
        return OSafeThread::SceneManager::GetSingleton()->ClaimActor(a_actor, std::string{ a_owner.c_str() });
    }

    bool ReleaseActor(RE::StaticFunctionTag*, RE::Actor* a_actor, RE::BSFixedString a_owner)
    {
        return OSafeThread::SceneManager::GetSingleton()->ReleaseActor(a_actor, std::string{ a_owner.c_str() });
    }

    bool ClaimActors(RE::StaticFunctionTag*, std::int32_t a_threadId, RE::BSFixedString a_owner,
                     std::vector<RE::Actor*> a_actors)
    {
        return OSafeThread::SceneManager::GetSingleton()->ClaimActors(a_threadId, std::string{ a_owner.c_str() },
                                                                      a_actors);
    }

    bool ReleaseActors(RE::StaticFunctionTag*, std::int32_t a_threadId, RE::BSFixedString a_owner,
                       std::vector<RE::Actor*> a_actors)
    {
        return OSafeThread::SceneManager::GetSingleton()->ReleaseActors(a_threadId, std::string{ a_owner.c_str() },
                                                                        a_actors);
    }

    RE::BSFixedString GetOwner(RE::StaticFunctionTag*, std::int32_t a_threadId)
    {
        return OSafeThread::SceneManager::GetSingleton()->GetOwner(a_threadId);
    }

    bool IsOwner(RE::StaticFunctionTag*, std::int32_t a_threadId, RE::BSFixedString a_owner)
    {
        return OSafeThread::SceneManager::GetSingleton()->IsOwner(a_threadId, std::string{ a_owner.c_str() });
    }
}

bool OSafeThread::Papyrus::Bind(RE::BSScript::IVirtualMachine* a_vm)
{
    if (!a_vm) {
        logger::critical("Papyrus::Bind - could not get the VM.");
        return false;
    }

    a_vm->RegisterFunction("GetVersion", kClass, GetPluginVersion);
    a_vm->RegisterFunction("GetLastStartedThreadId", kClass, GetLastStartedThreadId);
    a_vm->RegisterFunction("CalmActor", kClass, CalmActor);
    a_vm->RegisterFunction("UncalmActor", kClass, UncalmActor);
    a_vm->RegisterFunction("IsActorCalmed", kClass, IsActorCalmed);
    a_vm->RegisterFunction("PrepareThread", kClass, PrepareThread);
    a_vm->RegisterFunction("BindThread", kClass, BindThread);
    a_vm->RegisterFunction("GetActors", kClass, GetSceneActors);
    a_vm->RegisterFunction("GetEnemies", kClass, GetEnemies);
    a_vm->RegisterFunction("GetAllies", kClass, GetAllies);
    a_vm->RegisterFunction("GetHostiles", kClass, GetHostiles);
    a_vm->RegisterFunction("GetNeutrals", kClass, GetNeutrals);
    a_vm->RegisterFunction("GetNearbyEnemies", kClass, GetNearbyEnemies);
    a_vm->RegisterFunction("GetNearbyAllies", kClass, GetNearbyAllies);
    a_vm->RegisterFunction("GetNearbyHostiles", kClass, GetNearbyHostiles);
    a_vm->RegisterFunction("GetNearbyNeutrals", kClass, GetNearbyNeutrals);
    a_vm->RegisterFunction("GetNearbyActors", kClass, GetNearbyBuffer);
    a_vm->RegisterFunction("GetCalmedActors", kClass, GetCalmedActors);
    a_vm->RegisterFunction("ClaimActor", kClass, ClaimActor);
    a_vm->RegisterFunction("ClaimActors", kClass, ClaimActors);
    a_vm->RegisterFunction("ReleaseActor", kClass, ReleaseActor);
    a_vm->RegisterFunction("ReleaseActors", kClass, ReleaseActors);
    a_vm->RegisterFunction("GetOwner", kClass, GetOwner);
    a_vm->RegisterFunction("IsOwner", kClass, IsOwner);

    logger::info("Registered OSafeThread Papyrus functions.");
    return true;
}
