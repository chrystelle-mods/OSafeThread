#pragma once

#include <unordered_set>

// OSafeThread — the "calmed" registry (KrisV-777 technique).
//
// A calmed actor is one OSafeThread has taken out of the detection/combat systems for the duration
// of a scene. The set here is just FormIDs; enforcement is the two game hooks in Hooks.cpp:
//   * DoDetect     — a calmed viewer/target can neither detect nor be detected
//   * UpdateCombat — a calmed actor is pulled out of combat every tick
//
// All access is on the main game thread (the hooks run there; Papyrus natives are deferred to
// it), so — like KrisV's ActorManager — no lock is taken on the hot detection path.
// NOTE: in-session only for now; co-save persistence comes with the make-safe path.

namespace OSafeThread::Calm
{
    inline std::unordered_set<RE::FormID>& Set()
    {
        static std::unordered_set<RE::FormID> set;
        return set;
    }

    inline void        Add(RE::FormID a_id) { Set().insert(a_id); }
    inline void        Remove(RE::FormID a_id) { Set().erase(a_id); }
    inline bool        Contains(RE::FormID a_id) { return Set().contains(a_id); }
    inline void        Clear() { Set().clear(); }
    inline std::size_t Count() { return Set().size(); }
}
