Scriptname OSafeThread Hidden

; Returns the installed OSafeThread version string, e.g. "1.0.0".
string Function GetVersion() global native

; Debug: the threadId of the most recent OStim thread OSafeThread observed starting,
; or -1 if none has started since the game loaded.
int Function GetLastStartedThreadId() global native

; --- Calm-engine functions ---
; Take an actor out of / back into the detection + combat systems.
; A calmed actor can't detect or be detected and is pulled out of combat.
Function CalmActor(Actor akActor) global native
Function UncalmActor(Actor akActor) global native
bool Function IsActorCalmed(Actor akActor) global native

; ============================================================================================
; Parallels OThreadBuilder.Start(builderId)
;
; Build the thread with OStim's ThreadBuilder and then call this INSTEAD of OThreadBuilder.Start.
;
; @param builderId : the id from OThreadBuilder.Create(...)
; @param actors    : the same actor array passed to threadbuilder
; @param ownerId   : optional mod id string. Pass it if you want to Claim/Release this thread's 
;                    actors later. Leave "" if you don't need to hold actors past the thread.
; ============================================================================================
int Function Start(int builderId, Actor[] actors, string ownerId = "") global
    PrepareThread(actors, ownerId)
    int threadId = OThreadBuilder.Start(builderId)
    BindThread(threadId)
    return threadId
endFunction

; ============================================================================================
; Parallels OThread.QuickStart(...)
;
; @param actors            : the thread actors
; @param startingAnimation : optional thread id to start in ("" => OStim picks one automatically)
; @param furnitureRef      : optional furniture ObjectReference (None => OStim picks/none)
; @param ownerId           : optional mod id for Claim/Release. "" => not claimable.
; ============================================================================================
int Function QuickStart(Actor[] actors, string startingAnimation = "", ObjectReference furnitureRef = None, string ownerId = "") global
    PrepareThread(actors, ownerId)
    int threadId = OThread.QuickStart(actors, startingAnimation, furnitureRef)
    BindThread(threadId)   ; threadId < 0 => BindThread rolls back the calm
    return threadId
endFunction

; Natives the wrapper orchestrates (you normally don't call these directly).
Function PrepareThread(Actor[] actors, string ownerId) global native
Function BindThread(int threadId) global native

; ============================================================================================
; Read API — call from within an OStim event listener, like ostim_thread_start and ostim_thread_end.
; ============================================================================================

; The thread participants OSafeThread is managing (empty if not managed).
Actor[] Function GetActors(int threadId) global native

; Nearby actors OSafeThread classified in the buffer around this thread:
;   Enemies  - potentially hostile to the player AND already in combat
;   Hostiles - potentially hostile to the player but NOT yet in combat (these + enemies get calmed)
;   Allies   - the player's teammates/followers
;   Neutrals - nearby NPCs that are neither potentially hostile nor teammates
; "Potentially hostile" = hostile to the player, OR an enemy by faction, OR very-aggressive and not an
; ally/friend (so an unaware bandit still counts). See Classify in the DLL for the exact union.
Actor[] Function GetEnemies(int threadId) global native
Actor[] Function GetAllies(int threadId) global native
Actor[] Function GetHostiles(int threadId) global native
Actor[] Function GetNeutrals(int threadId) global native

; ============================================================================================
; Pre-thread classification of nearby actors to help in deciding what actor array to pass to thread
; builder. Same buckets/definitions as the readers above, but keyed by a center reference instead of a 
; threadId (which you don't have yet). 
; akCenter = where to scan (None => the player)
; afRadius = radius in units ; (0.0 => OSafeThread's configured buffer)
; Classification will still be player-relative even if akCenter sets a different actor as the scan origin
; ============================================================================================
Actor[] Function GetNearbyEnemies(ObjectReference akCenter = None, float afRadius = 0.0) global native
Actor[] Function GetNearbyAllies(ObjectReference akCenter = None, float afRadius = 0.0) global native
Actor[] Function GetNearbyHostiles(ObjectReference akCenter = None, float afRadius = 0.0) global native
Actor[] Function GetNearbyNeutrals(ObjectReference akCenter = None, float afRadius = 0.0) global native
Actor[] Function GetNearbyActors(ObjectReference akCenter = None, float afRadius = 0.0) global native

; Every actor OSafeThread is currently calming for this thread (the thread participants PLUS the
; enemies/hostiles it pulled out of combat in the buffer). Pass afMaxDistance to keep only those
; within that many units of the thread (<= 0.0 returns all). 
; You can feed the returned array into ClaimActors.
Actor[] Function GetCalmedActors(int threadId, float afMaxDistance = 0.0) global native

; Claim / release a set of this thread's calmed actors (thread participants + the calmed
; enemies/hostiles from GetCalmedActors). Leave `actors` unset to default to the thread participants.
; Typical flow for nearby hostiles:
;   Actor[] near = OSafeThread.GetCalmedActors(threadId, 512.0)   ; only the very close ones
;   OSafeThread.ClaimActors(threadId, "MyMod", near)             ; hold them past thread end
;   ; ...do whatever your mod wants to do with the actor
;   OSafeThread.ReleaseActors(threadId, "MyMod", near)           ; free them, no re-aggro
bool Function ClaimActors(int threadId, string ownerId, Actor[] actors = None) global native
bool Function ReleaseActors(int threadId, string ownerId, Actor[] actors = None) global native

; Claim / release one thread participant.
bool Function ClaimActor(Actor akActor, string ownerId) global native
bool Function ReleaseActor(Actor akActor, string ownerId) global native

string Function GetOwner(int threadId) global native            ; the ownerId that started this thread ("" if none/unmanaged)
bool   Function IsOwner(int threadId, string ownerId) global native
