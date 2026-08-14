#include "UI.h"

#include <filesystem>  // SKSEMenuFramework.h's IsInstalled() uses std::filesystem

#include "Config.h"
#include "SceneManager.h"
#include "SKSEMenuFramework.h"  // vendored: extern/SKSEMenuFramework (header-only, soft-links the DLL)

// This framework fork renames the ImGui namespace to ImGuiMCP (so it can't clash with other
// ImGui-using plugins) — its functions resolve the DLL's cimgui `ig*` exports at call time. Alias it
// back to the conventional name so standard ImGui snippets read normally in this translation unit.
namespace ImGui = ImGuiMCP;

namespace
{
    using OSafeThread::Config;

    // Runs on the framework's ImGui render thread. Edits the Config floats in place; SceneManager
    // reads them on the main thread (see Config.h for why no lock is needed).
    void __stdcall RenderSettings()
    {
        auto* cfg = Config::GetSingleton();

        ImGui::Text("Calm buffer radius");
        ImGui::SliderFloat("units##ost_radius", &cfg->bufferRadius, Config::kMinBufferRadius,
                           Config::kMaxBufferRadius, "%.0f");
        ImGui::TextWrapped("How far around a scene OSafeThread pulls nearby hostiles out of combat. "
                           "Default 4096 (~one exterior cell, ~57 m). Takes effect on the next scene.");
        ImGui::Spacing();

        ImGui::Text("Unclaimed restore delay");
        ImGui::SliderFloat("seconds##ost_delay", &cfg->unclaimedRestoreDelay, Config::kMinRestoreDelay,
                           Config::kMaxRestoreDelay, "%.1f");
        ImGui::TextWrapped("After a scene ends, how long before a calmed actor the starting mod did NOT "
                           "claim is returned to normal. Default 5. Claimed actors are unaffected.");
        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Save")) {
            cfg->Save();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to defaults")) {
            cfg->ResetDefaults();
            cfg->Save();
        }
        ImGui::SameLine();
        ImGui::TextWrapped("(Save writes Data/SKSE/Plugins/OSafeThread.ini)");

        // ---- Live status (read-only) ----
        ImGui::Spacing();
        ImGui::SeparatorText("Live status");

        const auto snap = OSafeThread::SceneManager::GetSingleton()->GetStatus();
        ImGui::Text("Managed scenes: %zu%s", snap.managedScenes, snap.hasPending ? "  (+1 pending bind)" : "");
        ImGui::Text("Calmed actors (total): %zu", snap.calmedTotal);
        ImGui::Text("Actors awaiting restore: %zu", snap.pendingRestores);

        if (snap.scenes.empty()) {
            ImGui::TextDisabled("No active managed scenes.");
        } else {
            for (const auto& s : snap.scenes) {
                ImGui::BulletText("thread %d  owner='%s'%s  |  actors %zu, calmed %zu, claimed %zu",
                                  s.threadId, s.owner.empty() ? "(none)" : s.owner.c_str(),
                                  s.ended ? " [ended]" : "", s.actors, s.calmed, s.claimed);
            }
        }
    }
}

void OSafeThread::UI::Register()
{
    if (!SKSEMenuFramework::IsInstalled()) {
        logger::info("UI: SKSE Menu Framework not installed — settings page not registered.");
        return;
    }
    SKSEMenuFramework::SetSection("OSafeThread");
    SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
    logger::info("UI: registered OSafeThread settings page (SKSE Menu Framework).");
}
