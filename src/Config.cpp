#include "Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    // Kept relative (like the framework's own IsInstalled path) so MO2's VFS maps it. Reads resolve
    // through the virtual Data tree; writes land in MO2's overwrite. The game's CWD is the Skyrim root.
    constexpr auto kIniPath = "Data/SKSE/Plugins/OSafeThread.ini";

    float Clamp(float a_v, float a_min, float a_max)
    {
        return std::clamp(a_v, a_min, a_max);
    }

    // Trim ASCII whitespace from both ends.
    std::string Trim(std::string a_s)
    {
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        a_s.erase(a_s.begin(), std::find_if(a_s.begin(), a_s.end(), notSpace));
        a_s.erase(std::find_if(a_s.rbegin(), a_s.rend(), notSpace).base(), a_s.end());
        return a_s;
    }
}

void OSafeThread::Config::Load()
{
    std::ifstream file(kIniPath);
    if (!file) {
        logger::info("Config: no ini at '{}', using defaults (radius {}, delay {}s).", kIniPath, bufferRadius,
                     unclaimedRestoreDelay);
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        const auto hash = line.find_first_of(";#");  // strip trailing/leading comments
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const auto key = Trim(line.substr(0, eq));
        const auto val = Trim(line.substr(eq + 1));
        if (val.empty()) {
            continue;
        }
        try {
            if (key == "BufferRadius") {
                bufferRadius = Clamp(std::stof(val), kMinBufferRadius, kMaxBufferRadius);
            } else if (key == "UnclaimedRestoreDelay") {
                unclaimedRestoreDelay = Clamp(std::stof(val), kMinRestoreDelay, kMaxRestoreDelay);
            }
        } catch (const std::exception&) {
            // malformed number -> keep the current (default) value for that key
        }
    }

    logger::info("Config: loaded radius {}, delay {}s from '{}'.", bufferRadius, unclaimedRestoreDelay, kIniPath);
}

void OSafeThread::Config::Save() const
{
    std::ofstream file(kIniPath, std::ios::trunc);
    if (!file) {
        logger::warn("Config: could not open '{}' for writing.", kIniPath);
        return;
    }
    file << "; OSafeThread settings. Edit in-game via the SKSE Menu Framework (Mod Control Panel ->\n"
         << "; OSafeThread -> Settings), or by hand here. Values are clamped on load.\n"
         << "BufferRadius = " << bufferRadius << "\n"
         << "UnclaimedRestoreDelay = " << unclaimedRestoreDelay << "\n";
    logger::info("Config: saved radius {}, delay {}s to '{}'.", bufferRadius, unclaimedRestoreDelay, kIniPath);
}

void OSafeThread::Config::ResetDefaults()
{
    bufferRadius          = kDefaultBufferRadius;
    unclaimedRestoreDelay = kDefaultRestoreDelay;
}
