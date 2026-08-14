#pragma once

#include <atomic>

#include "SceneManager.h"

// OSafeThread — observe OStim's thread lifecycle.
//
// OStim announces scenes as standard SKSE mod events (see OStim GameEvents.cpp):
//   "ostim_thread_start"  numArg = threadID
//   "ostim_thread_end"    numArg = threadID, strArg = JSON payload
// These flow through SKSE::GetModCallbackEventSource() — the same channel Papyrus
// RegisterForModEvent uses. Phase 0: log that we caught them and remember the last
// started threadId so a Papyrus probe can read it back. No calming yet.

namespace OSafeThread
{
    class ThreadEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
    {
    public:
        [[nodiscard]] static ThreadEventSink* GetSingleton()
        {
            static ThreadEventSink singleton;  // static lifetime: the source holds an unowned ptr, so we never destroy it
            return &singleton;
        }

        // The mod-callback source is a static SKSE dispatcher, never null after SKSE::Init,
        // so this is safe to call from SKSEPlugin_Load. AddEventSink is idempotent + null-safe.
        static void Register()
        {
            if (auto* source = SKSE::GetModCallbackEventSource()) {
                source->AddEventSink(GetSingleton());
                logger::info("ThreadEventSink registered on the mod-callback event source.");
            } else {
                logger::error("Mod-callback event source unavailable - ThreadEventSink NOT registered.");
            }
        }

        // Phase-0 probe: threadId of the most recent ostim_thread_start we observed (-1 if none yet).
        [[nodiscard]] static std::int32_t LastStartedThreadId() { return _lastStartedThreadId.load(); }

        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event,
                                              RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
        {
            if (!a_event) {  // mandatory: SendEvent forwards the pointer without null-checking it
                return RE::BSEventNotifyControl::kContinue;
            }

            if (a_event->eventName == "ostim_thread_start"sv) {
                const auto threadId = static_cast<std::int32_t>(a_event->numArg);
                _lastStartedThreadId.store(threadId);
                logger::info("[ostim_thread_start] threadId={}", threadId);
            } else if (a_event->eventName == "ostim_thread_end"sv) {
                const auto threadId = static_cast<std::int32_t>(a_event->numArg);
                logger::info("[ostim_thread_end] threadId={} payload='{}'", threadId, a_event->strArg.c_str());
                // Restore (uncalm) any actors OSafeThread managed for this scene.
                // TODO: honor a configurable restore delay (default ~5s) instead of restoring immediately.
                SceneManager::GetSingleton()->RestoreThread(threadId);
            }

            return RE::BSEventNotifyControl::kContinue;  // never starve other listeners of the event
        }

    private:
        ThreadEventSink() = default;
        ~ThreadEventSink() override = default;
        ThreadEventSink(const ThreadEventSink&) = delete;
        ThreadEventSink(ThreadEventSink&&) = delete;
        ThreadEventSink& operator=(const ThreadEventSink&) = delete;
        ThreadEventSink& operator=(ThreadEventSink&&) = delete;

        static inline std::atomic<std::int32_t> _lastStartedThreadId{ -1 };
    };
}
