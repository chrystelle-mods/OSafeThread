#pragma once

// OSafeThread — game-code hooks that enforce the "calmed" state (see Calm.h).
// Both target stock game code via Address Library, so they're robust across OStim updates.

namespace OSafeThread::Hooks
{
    // Install the detection + combat hooks. Call once, from kPostLoad.
    void Install();
}
