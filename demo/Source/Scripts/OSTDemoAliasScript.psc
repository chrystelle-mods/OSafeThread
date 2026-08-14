Scriptname OSTDemoAliasScript extends ReferenceAlias

int Property kVKey auto

Event OnInit()
    Maintenance()
EndEvent

Event OnPlayerLoadGame()
    Maintenance()                        ; SKSE key regs are lost on reload - re-register here
EndEvent

Function Maintenance()
    RegisterForKey(kVKey)
    Debug.Notification("OSTDemo ready - press V near an actor")
EndFunction

Event OnKeyDown(int aiKeyCode)
    if aiKeyCode != kVKey || Utility.IsInMenuMode()
        return
    endif

    Actor player = Game.GetPlayer()
    Actor target = FindNearestNPC(player, 2048.0)
    if !target
        Debug.Notification("OSTDemo: no actor nearby")
        return
    endif

    Actor[] party = new Actor[2]
    party[0] = player
    party[1] = target
    int threadId = OSafeThread.QuickStart(party)
    Debug.Notification("OSTDemo: QuickStart -> thread " + threadId)
EndEvent

Actor Function FindNearestNPC(Actor akFrom, float afRadius)
    Actor[] nearby = OSafeThread.GetNearbyActors(akFrom, afRadius)
    if nearby.Length == 0
        return None
    endif
    return OSANative.SortActorsByDistance(akFrom, nearby)[0]
EndFunction
