#include "PCH.h"

#include "Config.h"
#include "Hooks.h"
#include "Papyrus.h"
#include "SceneManager.h"
#include "ThreadEventSink.h"
#include "UI.h"

using namespace SKSE;
using namespace SKSE::log;
using namespace SKSE::stl;

namespace {
    void InitializeLogging() {
        auto path = log_directory();
        if (!path) {
            report_and_fail("OSafeThread: unable to lookup SKSE logs directory."sv);
        }
        *path /= PluginDeclaration::GetSingleton()->GetName();  // OSafeThread.log
        *path += L".log";

        std::shared_ptr<spdlog::logger> log;
        if (IsDebuggerPresent()) {
            log = std::make_shared<spdlog::logger>("Global", std::make_shared<spdlog::sinks::msvc_sink_mt>());
        } else {
            log = std::make_shared<spdlog::logger>(
                "Global", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
        }
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
            case SKSE::MessagingInterface::kPostLoad:
                // Load user settings before any scene can read them, then install the
                // detection/combat hooks — pure engine-code hooks, safe here.
                OSafeThread::Config::GetSingleton()->Load();
                OSafeThread::Hooks::Install();
                break;
            case SKSE::MessagingInterface::kPostLoadGame:
            case SKSE::MessagingInterface::kNewGame:
                // Calm must not survive a reload (OStim threads don't persist either). Clear it so
                // no actor is left stuck-calmed after loading away from an active scene.
                OSafeThread::SceneManager::GetSingleton()->Reset();
                break;
            case SKSE::MessagingInterface::kDataLoaded:
                // All plugins are loaded now, so the SKSE Menu Framework DLL (if present) is in the
                // process — safe to register our settings page. No-op when it isn't installed.
                OSafeThread::UI::Register();
                logger::info("kDataLoaded reached — OSafeThread is live.");
                break;
            default:
                break;
        }
    }
}  // namespace

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion(REL::Version("1.0.0.0"sv));
    v.PluginName("OSafeThread");
    v.AuthorName("Lacey");
    v.UsesAddressLibrary();  // one DLL across SE / AE / VR
    v.UsesNoStructs();
    return v;
}();

extern "C" DLLEXPORT bool SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* a_info) {
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = "OSafeThread";
    a_info->version = 0x01000000;  // 1.0.0.0 packed
    return true;
}

extern "C" DLLEXPORT bool SKSEPlugin_Load(const LoadInterface* a_skse) {
    InitializeLogging();

    auto* plugin = PluginDeclaration::GetSingleton();
    logger::info("{} {} is loading...", plugin->GetName(), plugin->GetVersion());

    Init(a_skse);

    if (!SKSE::GetMessagingInterface()->RegisterListener(MessageHandler)) {
        return false;
    }

    // Listen for OStim's thread-lifecycle mod events. The mod-callback source is a static
    // SKSE dispatcher (never null after Init), so registering here in Load is safe.
    OSafeThread::ThreadEventSink::Register();

    // Expose OSafeThread' native Papyrus functions.
    if (!SKSE::GetPapyrusInterface()->Register(OSafeThread::Papyrus::Bind)) {
        stl::report_and_fail("OSafeThread: failed to register Papyrus functions."sv);
    }

    logger::info("{} finished loading.", plugin->GetName());
    return true;
}
