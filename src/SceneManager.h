#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// OSafeThread — tracks the scenes OSafeThread is managing, keyed by OStim threadId.
//
// Ownership: the starting mod may pass an ownerId (via Start). Only that owner can
// claim/release the scene's actors. Claimed actors are exempt from auto-restore on thread end —
// the owner releases them on its own schedule (e.g. after a taming hand-off). Reads are open by
// threadId. (A configurable restore delay + hard-limit timer is a separate, later addition.)

namespace OSafeThread
{
    struct SceneRecord
    {
        std::string                    owner;           // mod-declared owner id ("" = unclaimable)
        bool                           ended{ false };  // thread has ended (claimed actors awaiting release)
        RE::NiPoint3                   origin{};        // scene anchor; distance filter for GetCalmedActors
        std::vector<RE::FormID>        actors;
        std::vector<RE::FormID>        enemies;
        std::vector<RE::FormID>        allies;
        std::vector<RE::FormID>        hostiles;
        std::vector<RE::FormID>        neutrals;
        std::vector<RE::FormID>        calmed;          // currently-calmed actors for this scene
        std::unordered_set<RE::FormID> claimed;         // scene actors the owner claimed (skip auto-restore)
    };

    // Read-only snapshot for the settings/status UI (see UI.cpp). Filled under the manager lock.
    struct SceneStatus
    {
        std::int32_t threadId;
        std::string  owner;
        bool         ended;
        std::size_t  actors;
        std::size_t  calmed;
        std::size_t  claimed;
    };
    struct StatusSnapshot
    {
        std::size_t              managedScenes;    // _scenes.size()
        std::size_t              calmedTotal;      // global Calm set size
        std::size_t              pendingRestores;  // actors on a restore countdown
        bool                     hasPending;       // a PrepareThread'd scene awaiting its threadId
        std::vector<SceneStatus> scenes;
    };

    class SceneManager
    {
    public:
        [[nodiscard]] static SceneManager* GetSingleton()
        {
            static SceneManager singleton;
            return &singleton;
        }

        void PrepareThread(const std::vector<RE::Actor*>& a_actors, const std::string& a_owner);
        void BindThread(std::int32_t a_threadId);
        void RestoreThread(std::int32_t a_threadId);  // thread end: SCHEDULE restores (5s unclaimed / 60s claimed)
        void Reset();

        // Advance the restore timers by a_deltaSeconds; called every frame from the update hook.
        void Tick(float a_deltaSeconds);

        // Ownership-gated writes (a_owner must match the scene's recorded owner; "" never matches).
        // Singular: claim/release one scene participant.
        bool ClaimActor(RE::Actor* a_actor, const std::string& a_owner);
        bool ReleaseActor(RE::Actor* a_actor, const std::string& a_owner);

        // Plural: claim/release a set of this scene's CALMED actors (scene participants + calmed
        // enemies/hostiles). An empty a_actors defaults to the scene participants (the array passed to
        // OStim). A requested actor that isn't calmed for this thread (e.g. an ally/non-hostile — those
        // are never calmed) is skipped and logged; the rest still process. Owner-gated.
        bool ClaimActors(std::int32_t a_threadId, const std::string& a_owner, const std::vector<RE::Actor*>& a_actors);
        bool ReleaseActors(std::int32_t a_threadId, const std::string& a_owner, const std::vector<RE::Actor*>& a_actors);

        // Open reads (by threadId).
        [[nodiscard]] std::string             GetOwner(std::int32_t a_threadId);
        [[nodiscard]] bool                    IsOwner(std::int32_t a_threadId, const std::string& a_owner);
        [[nodiscard]] std::vector<RE::Actor*> GetActors(std::int32_t a_threadId);
        [[nodiscard]] std::vector<RE::Actor*> GetEnemies(std::int32_t a_threadId);
        [[nodiscard]] std::vector<RE::Actor*> GetAllies(std::int32_t a_threadId);
        [[nodiscard]] std::vector<RE::Actor*> GetHostiles(std::int32_t a_threadId);
        [[nodiscard]] std::vector<RE::Actor*> GetNeutrals(std::int32_t a_threadId);
        // Currently-calmed actors for this scene. a_maxDistance > 0 keeps only those within that many
        // units of the scene origin; <= 0 returns all. Lets a mod target only very-near bystanders.
        [[nodiscard]] std::vector<RE::Actor*> GetCalmedActors(std::int32_t a_threadId, float a_maxDistance);

        // Live counts + per-scene rows for the status UI.
        [[nodiscard]] StatusSnapshot GetStatus();

    private:
        SceneManager() = default;

        // Find the managed thread whose scene actors include a_actorId; -1 if none. Caller holds _mutex.
        std::int32_t FindThreadOfActor(RE::FormID a_actorId);
        // Uncalm one actor of a record and drop it from claimed/calmed (+ cancel its timer). Caller holds _mutex.
        void ReleaseOne(SceneRecord& a_rec, RE::FormID a_actorId);
        // Uncalm a scheduled actor: find its record, drop it, erase the record if it's ended + empty. Caller holds _mutex.
        void FireRestore(RE::FormID a_actorId);

        std::mutex                                    _mutex;
        SceneRecord                                   _pending;
        bool                                          _hasPending{ false };
        std::unordered_map<std::int32_t, SceneRecord> _scenes;
        std::unordered_map<RE::FormID, float>         _restoreTimers;  // actorId -> seconds until auto-restore
    };
}
