module CyberpunkMP.Ink

import CyberpunkMP.World.*

// The mod's overlay shows the party list (colours, distances, host controls) while the pause menu or the world map
// is open.

@wrapMethod(PauseMenuGameController)
protected cb func OnInitialize() -> Bool {
    let result = wrappedMethod();
    GameInstance.GetNetworkWorldSystem().SetPauseMenuVisible(true);
    return result;
}

@wrapMethod(PauseMenuGameController)
protected cb func OnUninitialize() -> Bool {
    GameInstance.GetNetworkWorldSystem().SetPauseMenuVisible(false);
    return wrappedMethod();
}

@wrapMethod(WorldMapMenuGameController)
protected cb func OnInitialize() -> Bool {
    let result = wrappedMethod();
    GameInstance.GetNetworkWorldSystem().SetPauseMenuVisible(true);
    return result;
}

@wrapMethod(WorldMapMenuGameController)
protected cb func OnUninitialize() -> Bool {
    GameInstance.GetNetworkWorldSystem().SetPauseMenuVisible(false);
    return wrappedMethod();
}

// Remote players' markers (world map and minimap) in their party colour. Every mappin controller refreshes its root
// state through this, and the state's style can reset the tint, so it is applied again each time.
@wrapMethod(BaseMappinBaseController)
protected final func UpdateRootState() -> Void {
    wrappedMethod();

    let mappin = this.GetMappin();
    if !IsDefined(mappin) {
        return;
    }
    let system = GameInstance.GetNetworkWorldSystem();
    if Equals(mappin.GetVariant(), gamedataMappinVariant.CPO_RemotePlayerVariant) {
        let colour = system.GetPlayerColor(mappin.GetEntityID());
        this.GetRootWidget().SetTintColor(new HDRColor(colour.X, colour.Y, colour.Z, 1.0));
        // "Map markers" off in Mod Settings: keep them invisible (restored when switched back on)
        if system.GetOption("mapMarkers") < 0.5 {
            this.GetRootWidget().SetOpacity(0.0);
        } else {
            if this.GetRootWidget().GetOpacity() < 0.01 {
                this.GetRootWidget().SetOpacity(1.0);
            }
        }
    } else {
        if Equals(mappin.GetVariant(), gamedataMappinVariant.CustomPositionVariant) {
            let slot = system.GetAppearanceSystem().PartyPingSlot(mappin.GetNewMappinID());
            if slot >= 0 {
                let colour = system.GetSlotColor(slot);
                this.GetRootWidget().SetTintColor(new HDRColor(colour.X, colour.Y, colour.Z, 1.0));
            }
        }
    }
}
