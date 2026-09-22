#include "Hooks.h"

#include "Calm.h"
#include "SceneManager.h"

namespace
{
    // --- Detection ---------------------------------------------------------------------------
    // Call-site hook inside the game's detection routine (RelocationID 41659/42742).
    // If either the looker or the looked-at is calmed, force "not detected" (detectval = -1000)
    // and skip the real check — so a calmed actor can neither detect nor be detected.
    // Signature byte-matches the game function (KrisV-777).
    struct DetectHook
    {
        static std::uint8_t* thunk(RE::Actor* a_viewer, RE::Actor* a_target, std::int32_t& a_detectval,
                                   std::uint8_t& a_unk04, std::uint8_t& a_unk05, std::uint32_t& a_unk06,
                                   RE::NiPoint3& a_pos, float& a_unk08, float& a_unk09, float& a_unk10)
        {
            if ((a_viewer && OSafeThread::Calm::Contains(a_viewer->GetFormID())) ||
                (a_target && OSafeThread::Calm::Contains(a_target->GetFormID()))) {
                a_detectval = -1000;
                return nullptr;
            }
            return func(a_viewer, a_target, a_detectval, a_unk04, a_unk05, a_unk06, a_pos, a_unk08, a_unk09, a_unk10);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    // --- Combat ------------------------------------------------------------------------------
    // Character vtable[0], UpdateCombat (the per-tick combat update). A calmed actor is yanked
    // out of combat and its normal combat update is skipped.
    // Slot is 0x0E4 on SE/AE, 0x0E6 on VR (see Install()) — the VR index still wants in-game
    // verification on a real VR install before we call VR "supported".
    struct CombatHook
    {
        static void thunk(RE::Character* a_this)
        {
            if (a_this && OSafeThread::Calm::Contains(a_this->GetFormID())) {
                if (a_this->IsInCombat()) {
                    a_this->StopCombat();
                }
                return;
            }
            func(a_this);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    // --- Per-frame tick ----------------------------------------------------------------------
    // Mirror OAR's main-update nullsub call-site (VariantID 35565/36564 + VariantOffset). The
    // thunk is a bare void() that drives the restore-timer scheduler each frame on the main
    // thread, then chains the original. Safe alongside OAR: write_call chaining means both run.
    struct FrameUpdateHook
    {
        static void thunk()
        {
            // g_deltaTime is a float global (OAR: *(float*)VariantID(...).address()).
            static REL::Relocation<std::uintptr_t> deltaTimeAddr{ REL::VariantID(523660, 410199, 0x30C3A08) };
            const float delta = *reinterpret_cast<float*>(deltaTimeAddr.address());
            OSafeThread::SceneManager::GetSingleton()->Tick(delta);
            func();
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };
}

void OSafeThread::Hooks::Install()
{
    SKSE::AllocTrampoline(28);  // two write_call<5> hooks (DoDetect + frame update); write_vfunc needs none
    auto& trampoline = SKSE::GetTrampoline();

    REL::Relocation<std::uintptr_t> detect{ REL::RelocationID(41659, 42742),
                                            REL::VariantOffset(0x526, 0x67B, 0x67B) };
    DetectHook::func = trampoline.write_call<5>(detect.address(), DetectHook::thunk);

    // UpdateCombat is Actor vtable slot 0x0E4 on SE/AE; Skyrim VR shifts it to 0x0E6.
    // Authoritative: CommonLibSSE-NG's own Actor::UpdateCombat uses RelocateVirtual(0x0E4, 0x0E6).
    REL::Relocation<std::uintptr_t> characterVtbl{ RE::Character::VTABLE[0] };
    const std::size_t updateCombatIdx = REL::Module::IsVR() ? 0x0E6 : 0x0E4;
    CombatHook::func = characterVtbl.write_vfunc(updateCombatIdx, CombatHook::thunk);

    REL::Relocation<std::uintptr_t> mainUpdate{ REL::VariantID(35565, 36564, 0x5BAB10) };
    FrameUpdateHook::func = trampoline.write_call<5>(
        mainUpdate.address() + REL::VariantOffset(0x748, 0xC26, 0x7EE).offset(), FrameUpdateHook::thunk);

    logger::info("OSafeThread hooks installed (detection, combat, per-frame tick).");
}
