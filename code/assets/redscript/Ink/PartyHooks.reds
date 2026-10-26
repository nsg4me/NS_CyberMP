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
    if IsDefined(mappin) && Equals(mappin.GetVariant(), gamedataMappinVariant.CPO_RemotePlayerVariant) {
        let colour = GameInstance.GetNetworkWorldSystem().GetPlayerColor(mappin.GetEntityID());
        this.GetRootWidget().SetTintColor(new HDRColor(colour.X, colour.Y, colour.Z, 1.0));
    }
}
