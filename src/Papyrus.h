#pragma once

// OSafeThread — Papyrus native function registration (the C++ producer side).
// The matching .psc lives at data/Source/Scripts/OSafeThread.psc (Scriptname "OSafeThread").

namespace OSafeThread::Papyrus
{
    // Handed to SKSE::GetPapyrusInterface()->Register(...) from SKSEPlugin_Load.
    bool Bind(RE::BSScript::IVirtualMachine* a_vm);
}
