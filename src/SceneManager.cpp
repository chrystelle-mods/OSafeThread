#include "SceneManager.h"

#include <algorithm>

#include "Actors.h"
#include "Calm.h"
#include "Config.h"

namespace
{
    // Buffer radius and the unclaimed-restore delay are user-tunable (see Config / the SKSE Menu
    // Framework page) and read live at scene-prep / thread-end time.
    constexpr float kClaimedHardLimit = 60.0f;  // seconds; force-free a claimed-but-never-released actor

    std::vector<RE::FormID> ToFormIDs(const std::vector<RE::Actor*>& a_actors)
    {
        std::vector<RE::FormID> out;
        out.reserve(a_actors.size());
        for (auto* actor : a_actors) {
            if (actor) {
                out.push_back(actor->GetFormID());
            }
        }
        return out;
    }

    std::vector<RE::Actor*> ToActors(const std::vector<RE::FormID>& a_ids)
    {
        std::vector<RE::Actor*> out;
        out.reserve(a_ids.size());
        for (const auto id : a_ids) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
                out.push_back(actor);
            }
        }
        return out;
    }

    bool Contains(const std::vector<RE::FormID>& a_vec, RE::FormID a_id)
    {
        return std::find(a_vec.begin(), a_vec.end(), a_id) != a_vec.end();
    }
}

void OSafeThread::SceneManager::PrepareThread(const std::vector<RE::Actor*>& a_actors, const std::string& a_owner)
{
    const auto cls = Actors::Classify(a_actors, Config::GetSingleton()->bufferRadius);

    SceneRecord rec;
    rec.owner      = a_owner;
    rec.origin     = cls.origin;
    rec.actors     = ToFormIDs(cls.actors);
    rec.enemies    = ToFormIDs(cls.enemies);
    rec.allies     = ToFormIDs(cls.allies);
    rec.hostiles   = ToFormIDs(cls.hostiles);
    rec.neutrals   = ToFormIDs(cls.neutrals);

    rec.calmed = rec.actors;
    rec.calmed.insert(rec.calmed.end(), rec.enemies.begin(), rec.enemies.end());
    rec.calmed.insert(rec.calmed.end(), rec.hostiles.begin(), rec.hostiles.end());

    const auto na = rec.actors.size(), ne = rec.enemies.size(), nh = rec.hostiles.size(),
               nal = rec.allies.size(), nn = rec.neutrals.size(), nc = rec.calmed.size();

    {
        std::scoped_lock lock(_mutex);
        for (const auto id : rec.calmed) {
            Calm::Add(id);
        }
        _pending    = std::move(rec);
        _hasPending = true;
    }

    logger::info("PrepareThread: owner='{}'; {} scene actor(s); buffer -> {} enemies, {} hostiles, {} allies, {} neutral; calmed {}.",
                 a_owner, na, ne, nh, nal, nn, nc);
}

void OSafeThread::SceneManager::BindThread(std::int32_t a_threadId)
{
    std::scoped_lock lock(_mutex);
    if (!_hasPending) {
        return;
    }
    // A negative threadId means the underlying OStim start failed (OThreadBuilder.Start / OThread.
    // QuickStart return -1). No thread will ever fire ostim_thread_end, so binding would strand the
    // pending actors calmed until reload — roll back the calm we applied in PrepareThread instead.
    // (threadId 0 is a VALID player thread, not a failure.)
    if (a_threadId < 0) {
        for (const auto id : _pending.calmed) {
            Calm::Remove(id);
        }
        logger::info("BindThread: start failed (threadId {}) — rolled back calm on {} actor(s).", a_threadId,
                     _pending.calmed.size());
        _pending    = SceneRecord{};
        _hasPending = false;
        return;
    }
    _scenes[a_threadId] = std::move(_pending);
    _pending            = SceneRecord{};
    _hasPending         = false;
    logger::info("BindThread: thread {} is now managed (owner='{}').", a_threadId, _scenes[a_threadId].owner);
}

void OSafeThread::SceneManager::RestoreThread(std::int32_t a_threadId)
{
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    if (it == _scenes.end()) {
        return;
    }
    auto& rec = it->second;
    rec.ended = true;

    // Schedule restores rather than uncalming immediately: unclaimed actors free after a short delay,
    // claimed actors are held until the owner releases them (or a hard limit as a safety net).
    const float unclaimedDelay = Config::GetSingleton()->unclaimedRestoreDelay;
    std::size_t claimed = 0, unclaimed = 0;
    for (const auto id : rec.calmed) {
        if (rec.claimed.contains(id)) {
            _restoreTimers[id] = kClaimedHardLimit;
            ++claimed;
        } else {
            _restoreTimers[id] = unclaimedDelay;
            ++unclaimed;
        }
    }
    logger::info("RestoreThread: thread {} ended - {} actor(s) restoring in {}s; {} claimed held (<= {}s).",
                 a_threadId, unclaimed, unclaimedDelay, claimed, kClaimedHardLimit);
}

void OSafeThread::SceneManager::Tick(float a_deltaSeconds)
{
    if (a_deltaSeconds <= 0.0f) {
        return;  // paused / no time elapsed
    }
    std::scoped_lock lock(_mutex);
    if (_restoreTimers.empty()) {
        return;
    }
    std::vector<RE::FormID> due;
    for (auto& [id, remaining] : _restoreTimers) {
        remaining -= a_deltaSeconds;
        if (remaining <= 0.0f) {
            due.push_back(id);
        }
    }
    for (const auto id : due) {
        _restoreTimers.erase(id);
        FireRestore(id);
    }
}

void OSafeThread::SceneManager::FireRestore(RE::FormID a_actorId)
{
    Calm::Remove(a_actorId);
    for (auto it = _scenes.begin(); it != _scenes.end(); ++it) {
        auto& rec = it->second;
        if (!Contains(rec.calmed, a_actorId)) {
            continue;
        }
        rec.claimed.erase(a_actorId);
        rec.calmed.erase(std::remove(rec.calmed.begin(), rec.calmed.end(), a_actorId), rec.calmed.end());
        if (rec.ended && rec.calmed.empty()) {
            _scenes.erase(it);
        }
        return;
    }
}

void OSafeThread::SceneManager::Reset()
{
    std::scoped_lock lock(_mutex);
    const auto sceneCount = _scenes.size();
    _scenes.clear();
    _restoreTimers.clear();
    _pending    = SceneRecord{};
    _hasPending = false;
    Calm::Clear();
    logger::info("Reset: dropped {} managed scene(s) and cleared the calm set.", sceneCount);
}

std::int32_t OSafeThread::SceneManager::FindThreadOfActor(RE::FormID a_actorId)
{
    for (const auto& [threadId, rec] : _scenes) {
        if (Contains(rec.actors, a_actorId)) {
            return threadId;
        }
    }
    return -1;
}

void OSafeThread::SceneManager::ReleaseOne(SceneRecord& a_rec, RE::FormID a_actorId)
{
    Calm::Remove(a_actorId);
    _restoreTimers.erase(a_actorId);  // cancel any pending auto-restore for this actor
    a_rec.claimed.erase(a_actorId);
    a_rec.calmed.erase(std::remove(a_rec.calmed.begin(), a_rec.calmed.end(), a_actorId), a_rec.calmed.end());
}

bool OSafeThread::SceneManager::ClaimActor(RE::Actor* a_actor, const std::string& a_owner)
{
    if (!a_actor || a_owner.empty()) {
        return false;
    }
    std::scoped_lock lock(_mutex);
    const auto threadId = FindThreadOfActor(a_actor->GetFormID());
    if (threadId < 0) {
        return false;
    }
    auto& rec = _scenes[threadId];
    if (rec.owner != a_owner) {
        return false;
    }
    rec.claimed.insert(a_actor->GetFormID());
    return true;
}

bool OSafeThread::SceneManager::ReleaseActor(RE::Actor* a_actor, const std::string& a_owner)
{
    if (!a_actor || a_owner.empty()) {
        return false;
    }
    std::scoped_lock lock(_mutex);
    const auto id = a_actor->GetFormID();
    for (auto it = _scenes.begin(); it != _scenes.end(); ++it) {
        auto& rec = it->second;
        if (rec.owner != a_owner || !rec.claimed.contains(id)) {
            continue;
        }
        ReleaseOne(rec, id);
        if (rec.ended && rec.claimed.empty()) {
            _scenes.erase(it);
        }
        return true;
    }
    return false;
}

namespace
{
    // The FormIDs a plural claim/release targets: the passed actors, or — when none are given — the
    // scene participants (the array the mod handed OStim). Caller holds the manager lock.
    std::vector<RE::FormID> ResolveTargets(const OSafeThread::SceneRecord& a_rec,
                                           const std::vector<RE::Actor*>&   a_actors)
    {
        if (a_actors.empty()) {
            return a_rec.actors;  // default: the scene participants
        }
        std::vector<RE::FormID> out;
        out.reserve(a_actors.size());
        for (auto* actor : a_actors) {
            if (actor) {
                out.push_back(actor->GetFormID());
            }
        }
        return out;
    }
}

bool OSafeThread::SceneManager::ClaimActors(std::int32_t a_threadId, const std::string& a_owner,
                                            const std::vector<RE::Actor*>& a_actors)
{
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    if (it == _scenes.end() || a_owner.empty() || it->second.owner != a_owner) {
        return false;
    }
    auto&       rec     = it->second;
    const auto  targets = ResolveTargets(rec, a_actors);
    std::size_t claimed = 0, skipped = 0;
    for (const auto id : targets) {
        if (!Contains(rec.calmed, id)) {
            // Only calmed actors (participants + calmed enemies/hostiles) can be claimed. An ally or
            // non-hostile is never calmed — skip just this one, keep going, and log it.
            logger::warn("ClaimActors: thread {} — actor {:08X} is not calmed; skipped (only scene actors "
                         "and calmed enemies/hostiles are claimable — allies/non-hostiles are not).",
                         a_threadId, id);
            ++skipped;
            continue;
        }
        rec.claimed.insert(id);
        // If the scene already ended, this actor is on the unclaimed auto-restore clock — promote it to
        // the claimed hard-limit so a claim landing just after thread end still holds it.
        if (const auto t = _restoreTimers.find(id); t != _restoreTimers.end()) {
            t->second = kClaimedHardLimit;
        }
        ++claimed;
    }
    logger::info("ClaimActors: thread {} — owner '{}' claimed {} of {} target actor(s); {} skipped (not calmed).",
                 a_threadId, a_owner, claimed, targets.size(), skipped);
    return true;
}

bool OSafeThread::SceneManager::ReleaseActors(std::int32_t a_threadId, const std::string& a_owner,
                                              const std::vector<RE::Actor*>& a_actors)
{
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    if (it == _scenes.end() || a_owner.empty() || it->second.owner != a_owner) {
        return false;
    }
    auto&       rec      = it->second;
    const auto  targets  = ResolveTargets(rec, a_actors);
    std::size_t released = 0;
    for (const auto id : targets) {
        if (!rec.claimed.contains(id)) {
            continue;  // not claimed by you (or already restored) — harmless no-op
        }
        ReleaseOne(rec, id);
        ++released;
    }
    logger::info("ReleaseActors: thread {} — owner '{}' released {} of {} target actor(s).", a_threadId, a_owner,
                 released, targets.size());
    if (rec.ended && rec.claimed.empty()) {
        _scenes.erase(it);
    }
    return true;
}

std::string OSafeThread::SceneManager::GetOwner(std::int32_t a_threadId)
{
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    return it != _scenes.end() ? it->second.owner : std::string{};
}

bool OSafeThread::SceneManager::IsOwner(std::int32_t a_threadId, const std::string& a_owner)
{
    if (a_owner.empty()) {
        return false;
    }
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    return it != _scenes.end() && it->second.owner == a_owner;
}

std::vector<RE::Actor*> OSafeThread::SceneManager::GetActors(std::int32_t a_threadId)
{
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    return it != _scenes.end() ? ToActors(it->second.actors) : std::vector<RE::Actor*>{};
}

std::vector<RE::Actor*> OSafeThread::SceneManager::GetEnemies(std::int32_t a_threadId)
{
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    return it != _scenes.end() ? ToActors(it->second.enemies) : std::vector<RE::Actor*>{};
}

std::vector<RE::Actor*> OSafeThread::SceneManager::GetAllies(std::int32_t a_threadId)
{
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    return it != _scenes.end() ? ToActors(it->second.allies) : std::vector<RE::Actor*>{};
}

std::vector<RE::Actor*> OSafeThread::SceneManager::GetHostiles(std::int32_t a_threadId)
{
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    return it != _scenes.end() ? ToActors(it->second.hostiles) : std::vector<RE::Actor*>{};
}

std::vector<RE::Actor*> OSafeThread::SceneManager::GetNeutrals(std::int32_t a_threadId)
{
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    return it != _scenes.end() ? ToActors(it->second.neutrals) : std::vector<RE::Actor*>{};
}

OSafeThread::StatusSnapshot OSafeThread::SceneManager::GetStatus()
{
    std::scoped_lock lock(_mutex);
    StatusSnapshot snap;
    snap.managedScenes   = _scenes.size();
    snap.calmedTotal     = Calm::Count();
    snap.pendingRestores = _restoreTimers.size();
    snap.hasPending      = _hasPending;
    snap.scenes.reserve(_scenes.size());
    for (const auto& [threadId, rec] : _scenes) {
        snap.scenes.push_back({ threadId, rec.owner, rec.ended, rec.actors.size(), rec.calmed.size(),
                                rec.claimed.size() });
    }
    return snap;
}

std::vector<RE::Actor*> OSafeThread::SceneManager::GetCalmedActors(std::int32_t a_threadId, float a_maxDistance)
{
    std::scoped_lock lock(_mutex);
    const auto it = _scenes.find(a_threadId);
    if (it == _scenes.end()) {
        return {};
    }
    const auto& rec = it->second;
    if (a_maxDistance <= 0.0f) {
        return ToActors(rec.calmed);  // no filter: every actor this scene is calming
    }
    const float             r2 = a_maxDistance * a_maxDistance;
    std::vector<RE::Actor*> out;
    out.reserve(rec.calmed.size());
    for (const auto id : rec.calmed) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (!actor) {
            continue;
        }
        const auto p  = actor->GetPosition();
        const auto dx = p.x - rec.origin.x, dy = p.y - rec.origin.y, dz = p.z - rec.origin.z;
        if (dx * dx + dy * dy + dz * dz <= r2) {
            out.push_back(actor);
        }
    }
    return out;
}
