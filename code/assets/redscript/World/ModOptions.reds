module CyberpunkMP.World

// The "NS-CyberMP" page in Mod Settings (pause menu / title screen > Mod Settings), plus game state the party panel
// shows (in a menu, in a cutscene, down). Values go to the mod (NetworkWorldSystem.SetOption), which saves them in
// settings.json so they also apply on the title screen. Without Mod Settings installed, settings.json is used as is.

public class NsOptionsChangedEvent extends Event {}

func NsB(value: Bool) -> Float {
    return value ? 1.0 : 0.0;
}

@if(ModuleExists("ModSettingsModule"))
func NsModSettingsAvailable() -> Bool {
    return true;
}

@if(!ModuleExists("ModSettingsModule"))
func NsModSettingsAvailable() -> Bool {
    return false;
}

@if(ModuleExists("ModSettingsModule"))
func NsRegisterModSettings(target: ref<IScriptable>) -> Void {
    ModSettings.RegisterListenerToClass(target);
    ModSettings.RegisterListenerToModifications(target);
}

@if(!ModuleExists("ModSettingsModule"))
func NsRegisterModSettings(target: ref<IScriptable>) -> Void {}

@if(ModuleExists("ModSettingsModule"))
func NsUnregisterModSettings(target: ref<IScriptable>) -> Void {
    ModSettings.UnregisterListenerToClass(target);
    ModSettings.UnregisterListenerToModifications(target);
}

@if(!ModuleExists("ModSettingsModule"))
func NsUnregisterModSettings(target: ref<IScriptable>) -> Void {}

public class NsCyberMpOptions extends ScriptableSystem {
    // --- Players ---

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Players")
    @runtimeProperty("ModSettings.category.order", "1")
    @runtimeProperty("ModSettings.displayName", "Nameplates")
    @runtimeProperty("ModSettings.description", "Friends' names over their heads, in their party colour.")
    public let nameplates: Bool = true;

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Players")
    @runtimeProperty("ModSettings.category.order", "1")
    @runtimeProperty("ModSettings.displayName", "Nameplate distance")
    @runtimeProperty("ModSettings.description", "Nameplates show for friends closer than this (metres).")
    @runtimeProperty("ModSettings.step", "25.0")
    @runtimeProperty("ModSettings.min", "25.0")
    @runtimeProperty("ModSettings.max", "1000.0")
    public let nameplateDistance: Float = 300.0;

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Players")
    @runtimeProperty("ModSettings.category.order", "1")
    @runtimeProperty("ModSettings.displayName", "Show distance on nameplates")
    @runtimeProperty("ModSettings.description", "Adds how far away each friend is under their name.")
    public let nameplateShowDistance: Bool = true;

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Players")
    @runtimeProperty("ModSettings.category.order", "1")
    @runtimeProperty("ModSettings.displayName", "Map markers")
    @runtimeProperty("ModSettings.description", "Friends' markers on the map and minimap.")
    public let mapMarkers: Bool = true;

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Players")
    @runtimeProperty("ModSettings.category.order", "1")
    @runtimeProperty("ModSettings.displayName", "Shared waypoints")
    @runtimeProperty("ModSettings.description", "Show waypoints friends place on their map (and share yours).")
    public let partyPings: Bool = true;

    // --- Interface ---

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Interface")
    @runtimeProperty("ModSettings.category.order", "2")
    @runtimeProperty("ModSettings.displayName", "Party panels")
    @runtimeProperty("ModSettings.description", "Party boxes on the title screen and the party list in the pause menu.")
    public let partyPanels: Bool = true;

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Interface")
    @runtimeProperty("ModSettings.category.order", "2")
    @runtimeProperty("ModSettings.displayName", "Info panel")
    @runtimeProperty("ModSettings.description", "Connection, version and log location in the pause menu.")
    public let infoPanel: Bool = true;

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Interface")
    @runtimeProperty("ModSettings.category.order", "2")
    @runtimeProperty("ModSettings.displayName", "Join/leave notifications")
    @runtimeProperty("ModSettings.description", "A message when someone joins or leaves the party.")
    public let toasts: Bool = true;

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Interface")
    @runtimeProperty("ModSettings.category.order", "2")
    @runtimeProperty("ModSettings.displayName", "Ping indicator")
    @runtimeProperty("ModSettings.description", "Connection dot and ping in the top-right corner while connected.")
    public let pingIndicator: Bool = true;

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Interface")
    @runtimeProperty("ModSettings.category.order", "2")
    @runtimeProperty("ModSettings.displayName", "Chat opacity")
    @runtimeProperty("ModSettings.description", "How visible the chat is while you are not typing.")
    @runtimeProperty("ModSettings.step", "0.1")
    @runtimeProperty("ModSettings.min", "0.0")
    @runtimeProperty("ModSettings.max", "1.0")
    public let chatOpacity: Float = 1.0;

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Interface")
    @runtimeProperty("ModSettings.category.order", "2")
    @runtimeProperty("ModSettings.displayName", "Chat size")
    @runtimeProperty("ModSettings.description", "Scale of the chat box and its text.")
    @runtimeProperty("ModSettings.step", "0.1")
    @runtimeProperty("ModSettings.min", "0.6")
    @runtimeProperty("ModSettings.max", "1.6")
    public let chatScale: Float = 1.0;

    // --- Troubleshooting ---

    @runtimeProperty("ModSettings.mod", "NS-CyberMP")
    @runtimeProperty("ModSettings.category", "Troubleshooting")
    @runtimeProperty("ModSettings.category.order", "3")
    @runtimeProperty("ModSettings.displayName", "Debug mode")
    @runtimeProperty("ModSettings.description", "Very detailed logging in CyberpunkMP.log. The launch option --debug-mode:1 / --debug-mode:0 overrides this.")
    public let debugMode: Bool = false;

    private let m_menuCallback: ref<CallbackHandle>;

    private func OnAttach() -> Void {
        NsRegisterModSettings(this);
        this.Push();

        let blackboard = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(GetAllBlackboardDefs().UI_System);
        if IsDefined(blackboard) {
            this.m_menuCallback = blackboard.RegisterListenerBool(GetAllBlackboardDefs().UI_System.IsInMenu, this, n"OnInMenuChanged");
        }
    }

    private func OnDetach() -> Void {
        NsUnregisterModSettings(this);
        let blackboard = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(GetAllBlackboardDefs().UI_System);
        if IsDefined(blackboard) && IsDefined(this.m_menuCallback) {
            blackboard.UnregisterListenerBool(GetAllBlackboardDefs().UI_System.IsInMenu, this.m_menuCallback);
        }
    }

    protected cb func OnInMenuChanged(value: Bool) -> Bool {
        GameInstance.GetNetworkWorldSystem().ReportActivity("menu", value);
    }

    public cb func OnModSettingsChange() -> Void {
        this.Push();
    }

    // Without Mod Settings the fields only hold defaults, so nothing is pushed and settings.json keeps deciding.
    private func Push() -> Void {
        if !NsModSettingsAvailable() {
            return;
        }
        let system = GameInstance.GetNetworkWorldSystem();
        system.SetOption("nameplates", NsB(this.nameplates));
        system.SetOption("nameplateDistance", this.nameplateDistance);
        system.SetOption("nameplateShowDistance", NsB(this.nameplateShowDistance));
        system.SetOption("mapMarkers", NsB(this.mapMarkers));
        system.SetOption("partyPings", NsB(this.partyPings));
        system.SetOption("partyPanels", NsB(this.partyPanels));
        system.SetOption("infoPanel", NsB(this.infoPanel));
        system.SetOption("toasts", NsB(this.toasts));
        system.SetOption("pingIndicator", NsB(this.pingIndicator));
        system.SetOption("chatOpacity", this.chatOpacity);
        system.SetOption("chatScale", this.chatScale);
        system.SetOption("debugMode", NsB(this.debugMode));
        GameInstance.GetUISystem(this.GetGameInstance()).QueueEvent(new NsOptionsChangedEvent());
    }
}

// --- Party status: cutscenes and being down ---------------------------------------------------------------------

@wrapMethod(PlayerPuppet)
protected cb func OnSceneTierChange(newState: Int32) -> Bool {
    let result = wrappedMethod(newState);
    if Equals(MpWho(this), "me") {
        // 3+ = limited gameplay / cinematic: the game is driving the player
        GameInstance.GetNetworkWorldSystem().ReportActivity("scene", newState >= 3);
    }
    return result;
}

@wrapMethod(PlayerPuppet)
protected cb func OnDeath(evt: ref<gameDeathEvent>) -> Bool {
    if Equals(MpWho(this), "me") {
        GameInstance.GetNetworkWorldSystem().ReportActivity("dead", true);
    }
    return wrappedMethod(evt);
}

// --- World map: follow a friend, share a waypoint -----------------------------------------------------------------

@wrapMethod(WorldMapMenuGameController)
private final func TryTrackQuestOrSetWaypoint() -> Void {
    // A friend's marker: track it like a normal waypoint, so the GPS route leads to them (and follows them).
    if IsDefined(this.selectedMappin) {
        let mappin = this.selectedMappin.GetMappin();
        if IsDefined(mappin) && Equals(mappin.GetVariant(), gamedataMappinVariant.CPO_RemotePlayerVariant) {
            this.UntrackCustomPositionMappin();
            this.TrackMappin(this.selectedMappin);
            this.PlaySound(n"MapPin", n"OnEnable");
            MpLog("Map", "tracking a friend's marker");
            return;
        }
    }

    wrappedMethod();

    // A waypoint the player just placed on an empty spot: share it with the party.
    let system = GameInstance.GetNetworkWorldSystem();
    if system.GetOption("partyPings") < 0.5 {
        return;
    }
    let mappinSystem = GameInstance.GetMappinSystem(this.GetPlayerControlledObject().GetGame());
    let tracked = mappinSystem.GetMappin(mappinSystem.GetManuallyTrackedMappinID());
    // (not when following a friend's shared waypoint: that one is theirs)
    if IsDefined(tracked) && Equals(tracked.GetVariant(), gamedataMappinVariant.CustomPositionVariant)
        && system.GetAppearanceSystem().PartyPingSlot(tracked.GetNewMappinID()) < 0 {
        system.SharePing(tracked.GetWorldPosition());
    }
}
