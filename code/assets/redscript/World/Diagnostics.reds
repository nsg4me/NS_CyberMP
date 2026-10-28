module CyberpunkMP.World

// Game events written to CyberpunkMP.log ([Script/...] lines), to see what was happening around a desync or crash:
// time dilation (Sandevistan, Kerenzikov), death, fast travel, scenes, combat, weapons and equipment.
// Details that fire often (every equipment slot, status effects) are only logged in debug mode.

func MpLog(category: String, message: String) -> Void {
    let system = GameInstance.GetNetworkWorldSystem();
    if IsDefined(system) {
        system.Log(category, message);
    }
}

func MpDebug() -> Bool {
    let system = GameInstance.GetNetworkWorldSystem();
    return IsDefined(system) && system.IsDebugMode();
}

// Record names are not available in the shipped game (ToStringDEBUG returns ""): fall back to the numeric id.
func MpId(id: TweakDBID) -> String {
    let name = TDBID.ToStringDEBUG(id);
    if StrLen(name) > 0 {
        return name;
    }
    return "0x" + ToString(TDBID.ToNumber(id));
}

func MpWho(puppet: ref<GameObject>) -> String {
    if !IsDefined(puppet) {
        return "?";
    }
    let player = GetPlayer(puppet.GetGame());
    if IsDefined(player) && Equals(player.GetEntityID(), puppet.GetEntityID()) {
        return "me";
    }
    return "game id " + EntityID.ToDebugString(puppet.GetEntityID());
}

// --- Time dilation ------------------------------------------------------------------------------------------------

@wrapMethod(TimeDilationEventsTransitions)
protected final func SetTimeDilationGlobal(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>, reason: CName, timeDilation: Float, opt duration: Float, opt easeInCurve: CName, opt easeOutCurve: CName, opt listener: ref<TimeDilationListener>) -> Void {
    MpLog("Time", "time dilation ON: " + NameToString(reason) + " scale " + FloatToString(timeDilation) + " for " + FloatToString(duration) + " s");
    wrappedMethod(stateContext, scriptInterface, reason, timeDilation, duration, easeInCurve, easeOutCurve, listener);
    MpLog("Time", "time dilation ON applied: " + NameToString(reason));
}

@wrapMethod(TimeDilationEventsTransitions)
protected final func SetTimeDilationOnLocalPlayer(reason: CName, timeDilation: Float, opt duration: Float, easeInCurve: CName, easeOutCurve: CName, stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
    MpLog("Time", "player time dilation ON: " + NameToString(reason) + " scale " + FloatToString(timeDilation));
    wrappedMethod(reason, timeDilation, duration, easeInCurve, easeOutCurve, stateContext, scriptInterface);
}

@wrapMethod(TimeDilationEventsTransitions)
protected final func UnsetTimeDilation(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>, reason: CName, opt easeOutCurve: CName, opt noDelayedCleanUp: Bool) -> Void {
    MpLog("Time", "time dilation OFF: " + NameToString(reason));
    wrappedMethod(stateContext, scriptInterface, reason, easeOutCurve, noDelayedCleanUp);
}

@wrapMethod(TimeDilationHelper)
public final static func SetTimeDilationWithProfile(requester: wref<GameObject>, const profileName: script_ref<String>, enable: Bool, allowMultipleTimeDilationSimultaneously: Bool) -> Bool {
    MpLog("Time", "time dilation profile " + Deref(profileName) + (enable ? " ON" : " OFF"));
    return wrappedMethod(requester, profileName, enable, allowMultipleTimeDilationSimultaneously);
}

@wrapMethod(TimeDilationHelper)
public final static func SetTimeDilation(requester: wref<GameObject>, reason: CName, timeDilation: Float, opt duration: Float, easeInCurve: CName, easeOutCurve: CName, allowMultipleTimeDilationSimultaneously: Bool, opt listener: ref<TimeDilationListener>) -> Bool {
    MpLog("Time", "time dilation (helper) " + NameToString(reason) + " scale " + FloatToString(timeDilation));
    return wrappedMethod(requester, reason, timeDilation, duration, easeInCurve, easeOutCurve, allowMultipleTimeDilationSimultaneously, listener);
}

@wrapMethod(SandevistanEvents)
protected func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
    MpLog("Time", "Sandevistan activating");
    wrappedMethod(stateContext, scriptInterface);
    MpLog("Time", "Sandevistan active");
}

@wrapMethod(SandevistanEvents)
protected func OnExit(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
    MpLog("Time", "Sandevistan ending");
    wrappedMethod(stateContext, scriptInterface);
}

// --- Player state -------------------------------------------------------------------------------------------------

@wrapMethod(PlayerPuppet)
protected cb func OnDeath(evt: ref<gameDeathEvent>) -> Bool {
    MpLog("Player", "death: " + MpWho(this));
    return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnCombatStateChanged(newState: Int32) -> Bool {
    MpLog("Player", "combat state " + IntToString(newState) + " (" + MpWho(this) + ")");
    return wrappedMethod(newState);
}

// Scene tiers: 1 = free play, 2+ = the game takes control (dialogue, cutscene). Remote characters in scenes desync.
@wrapMethod(PlayerPuppet)
protected cb func OnSceneTierChange(newState: Int32) -> Bool {
    MpLog("Player", "scene tier " + IntToString(newState) + " (" + MpWho(this) + ")");
    return wrappedMethod(newState);
}

@wrapMethod(PlayerPuppet)
protected cb func OnItemAddedToSlot(evt: ref<ItemAddedToSlot>) -> Bool {
    let slot = evt.GetSlotID();
    if MpDebug() || Equals(slot, t"AttachmentSlots.WeaponRight") || Equals(slot, t"AttachmentSlots.WeaponLeft") {
        MpLog("Equip", MpWho(this) + ": " + MpId(ItemID.GetTDBID(evt.GetItemID())) + " -> " + MpId(slot));
    }
    return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnItemRemovedFromSlot(evt: ref<ItemRemovedFromSlot>) -> Bool {
    let slot = evt.GetSlotID();
    if MpDebug() || Equals(slot, t"AttachmentSlots.WeaponRight") || Equals(slot, t"AttachmentSlots.WeaponLeft") {
        MpLog("Equip", MpWho(this) + ": " + MpId(ItemID.GetTDBID(evt.GetItemID())) + " removed from " + MpId(slot));
    }
    return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnStatusEffectApplied(evt: ref<ApplyStatusEffectEvent>) -> Bool {
    if MpDebug() && IsDefined(evt.staticData) {
        MpLog("Status", MpWho(this) + ": + " + MpId(evt.staticData.GetID()));
    }
    return wrappedMethod(evt);
}

@wrapMethod(PlayerPuppet)
protected cb func OnStatusEffectRemoved(evt: ref<RemoveStatusEffect>) -> Bool {
    if MpDebug() && IsDefined(evt.staticData) {
        MpLog("Status", MpWho(this) + ": - " + MpId(evt.staticData.GetID()));
    }
    return wrappedMethod(evt);
}

// --- World ----------------------------------------------------------------------------------------------------------

@wrapMethod(FastTravelSystem)
private final func PerformFastTravel(player: ref<GameObject>, nodeData: ref<FastTravelPointData>) -> Void {
    MpLog("World", "fast travel started");
    wrappedMethod(player, nodeData);
}

@wrapMethod(HUDManager)
protected cb func OnBraindanceToggle(value: Bool) -> Bool {
    MpLog("World", "braindance " + (value ? "started" : "ended"));
    return wrappedMethod(value);
}
