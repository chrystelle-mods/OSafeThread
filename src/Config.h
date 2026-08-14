#pragma once

// OSafeThread — user-tunable settings, edited via the SKSE Menu Framework page (see UI.cpp) and
// persisted to Data/SKSE/Plugins/OSafeThread.ini. Two values are exposed for now:
//   * bufferRadius          — how far around a scene we calm hostiles (default 4096 ~ one ext. cell)
//   * unclaimedRestoreDelay — seconds after a scene ends before an un-claimed calmed actor restores
//
// Plain floats, not atomics: they're read on the main thread (scene prep / restore scheduling) and
// written from the ImGui render thread. A 4-byte aligned float load/store is atomic on x86, and the
// values are advisory (worst case the next scene sees the pre-edit value), so no lock is needed.
// The claimed-actor hard limit stays a compile-time constant (not user-facing).

namespace OSafeThread
{
    class Config
    {
    public:
        static constexpr float kDefaultBufferRadius = 4096.0f;  // ~57 m, ~one exterior cell
        static constexpr float kDefaultRestoreDelay = 5.0f;     // seconds

        static constexpr float kMinBufferRadius = 256.0f;
        static constexpr float kMaxBufferRadius = 16384.0f;     // ~4 exterior cells
        static constexpr float kMinRestoreDelay = 0.0f;
        static constexpr float kMaxRestoreDelay = 60.0f;        // matches the claimed hard-limit

        [[nodiscard]] static Config* GetSingleton()
        {
            static Config singleton;
            return &singleton;
        }

        float bufferRadius{ kDefaultBufferRadius };
        float unclaimedRestoreDelay{ kDefaultRestoreDelay };

        void Load();                 // read the ini (missing file => keep defaults); clamps values
        void Save() const;           // write the ini
        void ResetDefaults();        // restore compiled-in defaults (does not save)

    private:
        Config() = default;
    };
}
