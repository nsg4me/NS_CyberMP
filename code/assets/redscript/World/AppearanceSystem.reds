module CyberpunkMP.World

import Codeware.*
import CyberpunkMP.*
import CyberpunkMP.Ink.*
import CyberpunkMP.Plugins.*

// @addMethod(ArcadeMachine)
// protected cb func OnPlayInDeviceCallbackEvent(evt: ref<PlayInDeviceCallbackEvent>) -> Bool {
//     if evt.wasPlayInDeviceSuccessful {
//         if IsDefined(GameInstance.GetAppearanceSystem().activeAnim) {
//             let workspotSystem: ref<WorkspotGameSystem> = GameInstance.GetWorkspotSystem(GetGameInstance());
//             workspotSystem.StopInDevice(GameInstance.GetAppearanceSystem().activeAnim);
//             GameInstance.GetDynamicEntitySystem().DeleteEntity(GameInstance.GetAppearanceSystem().activeAnim.GetEntityID());
//             GameInstance.GetAppearanceSystem().activeAnim = null;
//         }
//     }
// }

@addField(GameObject)
public let animEntity: wref<GameObject>;

@addField(GameObject)
public let animName: CName;

@addField(GameObject)
public let animCallbackId: DelayID;

public class AnimationHackCallback extends DelayCallback {
    public let entity: wref<GameObject>;
    public func Call() -> Void {
        if IsDefined(this.entity.animEntity) {
            let workspotSystem: ref<WorkspotGameSystem> = GameInstance.GetWorkspotSystem(GetGameInstance());
            // let direction = new Vector4(0.00, 0.00, 0.00, 1.00);
            // let direction = this.entity.GetWorldPosition();
            // direction.X = -1.0 * direction.X;
            // direction.Y = -1.0 * direction.Y;
            // workspotSystem.StopInDevice(this.entity, direction);
            workspotSystem.StopInDevice(this.entity);
            GameInstance.GetDynamicEntitySystem().DeleteEntity(this.entity.animEntity.GetEntityID());
            this.entity.animEntity = null;
        }
    }
}

public native class AppearanceSystem extends IScriptable {
    public native func GetEntityItems(entityID: EntityID) -> array<TweakDBID>;
    //public native func SetMorphWeights(player: ref<GameObject>) -> Void;
    public native func ApplyAppearance(player: ref<GameObject>) -> Bool;

    private let m_callbackSystem: wref<CallbackSystem>;
    public let m_entities: array<wref<GameObject>>;
    public let m_controller: wref<MultiplayerGameController>;
    public let m_deliveryEntries: array<ref<JobEntry>>;
    public let m_mappin: NewMappinID;

    // Map marker of each remote player's character, so it can be removed with the character.
    private let m_playerMappinEntities: array<EntityID>;
    private let m_playerMappins: array<NewMappinID>;

    public func RemovePlayerMappin(entityId: EntityID) -> Void {
        let mappinSystem = GameInstance.GetMappinSystem(GetGameInstance());
        let i = ArraySize(this.m_playerMappinEntities) - 1;
        while i >= 0 {
            if Equals(this.m_playerMappinEntities[i], entityId) {
                mappinSystem.UnregisterMappin(this.m_playerMappins[i]);
                ArrayErase(this.m_playerMappinEntities, i);
                ArrayErase(this.m_playerMappins, i);
            }
            i -= 1;
        }
    }

    // Waypoints shared by party members (one per colour slot 0-3), shown at a fixed spot in their colour.
    private let m_pingMappins: array<NewMappinID>;
    private let m_pingSlots: array<Int32>;

    public func ShowPartyPing(slot: Int32, position: Vector4) -> Void {
        this.ClearPartyPing(slot);
        let mappinData: MappinData;
        mappinData.mappinType = t"Mappins.QuestDynamicMappinDefinition";
        mappinData.variant = gamedataMappinVariant.CustomPositionVariant;
        mappinData.active = true;
        mappinData.visibleThroughWalls = true;
        let id = GameInstance.GetMappinSystem(GetGameInstance()).RegisterMappin(mappinData, position);
        ArrayPush(this.m_pingMappins, id);
        ArrayPush(this.m_pingSlots, slot);
        MpLog("Map", "showing party waypoint for slot " + IntToString(slot));
    }

    public func ClearPartyPing(slot: Int32) -> Void {
        let mappinSystem = GameInstance.GetMappinSystem(GetGameInstance());
        let i = ArraySize(this.m_pingSlots) - 1;
        while i >= 0 {
            if this.m_pingSlots[i] == slot {
                mappinSystem.UnregisterMappin(this.m_pingMappins[i]);
                ArrayErase(this.m_pingMappins, i);
                ArrayErase(this.m_pingSlots, i);
            }
            i -= 1;
        }
    }

    // Colour slot of a shared waypoint marker, or -1 when the marker is not one.
    public func PartyPingSlot(id: NewMappinID) -> Int32 {
        let i = 0;
        while i < ArraySize(this.m_pingMappins) {
            if this.m_pingMappins[i].value == id.value {
                return this.m_pingSlots[i];
            }
            i += 1;
        }
        return -1;
    }

    public func RemoveAllPlayerMappins() -> Void {
        let mappinSystem = GameInstance.GetMappinSystem(GetGameInstance());
        for id in this.m_playerMappins {
            mappinSystem.UnregisterMappin(id);
        }
        ArrayClear(this.m_playerMappinEntities);
        ArrayClear(this.m_playerMappins);
        for id in this.m_pingMappins {
            mappinSystem.UnregisterMappin(id);
        }
        ArrayClear(this.m_pingMappins);
        ArrayClear(this.m_pingSlots);
    }

    public func OnWorldAttached() -> Void {
        this.m_callbackSystem = GameInstance.GetCallbackSystem();
        // if IsDefined(this.m_callbackSystem) {
        this.m_callbackSystem.RegisterCallback(n"Entity/Attached", this, n"OnEntityAttached")
            .AddTarget(DynamicEntityTarget.Tag(n"CyberpunkMP.Puppet"));
        //     LogChannel(n"DEBUG", s"[AppearanceSystem] Entity/Attached callback registered");
        // } else {            
        //     LogChannel(n"DEBUG", s"[AppearanceSystem] No CallbackSystem");
        // }
        
        this.m_callbackSystem.RegisterCallback(n"Entity/Attached", this, n"OnAnimationEntityAttached")
            .AddTarget(DynamicEntityTarget.Tag(n"CyberpunkMP.Puppet.Animation"));
    }

    public func OnBeforeWorldDetach() -> Void {
        this.RemoveAllPlayerMappins();
        if IsDefined(this.m_callbackSystem) {
            this.m_callbackSystem.UnregisterCallback(n"Entity/Attached", this);
        }
    }

    // The worn (visual) item in each paper-doll slot. Sent as TweakDB ids: their names are not available in the
    // shipped game (ToStringDEBUG returns ""), which is how all items used to arrive as 0.
    private func GetPlayerItems() -> array<TweakDBID> {
        let player = GetPlayer(GetGameInstance());
        let items: array<TweakDBID>;
        let equipData: ref<EquipmentSystemPlayerData> = EquipmentSystem.GetData(player);
        let equipAreas: array<SEquipArea>;
        if IsDefined(equipData) {
            equipAreas = equipData.GetPaperDollEquipAreas();
        }
        let i: Int32 = 0;
        while i < ArraySize(equipAreas) {
            let item = equipData.GetVisualItemInSlot(equipAreas[i].areaType);
            let tdbid = ItemID.GetTDBID(item);
            if TDBID.IsValid(tdbid) {
                ArrayPush(items, tdbid);
            }
            i += 1;
        }
        return items;
    }

    public let selected_entity_id: EntityID;
    
    public func HandleEmote(id: EntityID, name: String) -> Void {    
        LogChannel(n"DEBUG", s"Handling Emote: \(name)");
        this.selected_entity_id = id;
        let entity = GameInstance.FindEntityByID(GetGameInstance(), id) as GameObject;

        if !IsDefined(entity) {
            LogChannel(n"DEBUG", s"Could not find entity.");
            return;
        }

        entity.animName = StringToName(name);

        let npcSpec = new DynamicEntitySpec();

        // npcSpec.templatePath = r"base\\amm_workspots\\entity\\workspot_anim.ent";
        npcSpec.templatePath = r"mods\\cyberpunkmp\\workspot_anim.ent";
        npcSpec.alwaysSpawned = true;
        npcSpec.position = entity.GetWorldPosition();
        // npcSpec.position = new Vector4(0.0, 0.0, 0.0, 0.0);

        let workspotSystem: ref<WorkspotGameSystem> = GameInstance.GetWorkspotSystem(GetGameInstance());
        
        if IsDefined(entity.animEntity) {
            GameInstance.GetDelaySystem(GetGameInstance()).CancelCallback(entity.animCallbackId);
            // let direction = new Vector4(0.00, 0.00, 0.00, 1.00);
            // let direction = entity.GetWorldPosition();
            // direction.X = -1.0 * direction.X;
            // direction.Y = -1.0 * direction.Y;
            // workspotSystem.StopInDevice(entity, direction);
            workspotSystem.StopInDevice(entity);
            GameInstance.GetDynamicEntitySystem().DeleteEntity(entity.animEntity.GetEntityID());
            entity.animEntity = null;
            // why?? only works sometimes
            // npcSpec.position.X = -1.0 * npcSpec.position.X;
            // npcSpec.position.Y = -1.0 * npcSpec.position.Y;
        }
        

        let orientation = entity.GetWorldOrientation();
        let angles = Quaternion.ToEulerAngles(orientation);
        // angles.Yaw += 180.0;
        npcSpec.orientation = EulerAngles.ToQuat(angles);
        // npcSpec.orientation = EulerAngles.ToQuat(new EulerAngles(0.0, angles.Yaw, 0.0));
        npcSpec.persistState = false;
        npcSpec.persistSpawn = false;
        npcSpec.tags = [n"CyberpunkMP.Puppet.Animation"];

        // this.activeAnimId = GameInstance.GetDynamicEntitySystem().CreateEntity(npcSpec);
        GameInstance.GetDynamicEntitySystem().CreateEntity(npcSpec);
    }

    private cb func OnAnimationEntityAttached(event: ref<EntityLifecycleEvent>) {
        LogChannel(n"DEBUG", s"Emote Entity Attached \(EntityID.GetHash(event.GetEntity().GetEntityID()))");
        let workspotSystem: ref<WorkspotGameSystem> = GameInstance.GetWorkspotSystem(GetGameInstance());
        let entity = GameInstance.FindEntityByID(GetGameInstance(), this.selected_entity_id) as GameObject;

        entity.animEntity = event.GetEntity() as GameObject;
        
        workspotSystem.PlayInDeviceSimple(entity.animEntity, entity, false, n"workspot_base", n"None", n"None", 0, WorkspotSlidingBehaviour.DontPlayAtResourcePosition, null);
        workspotSystem.SendJumpToAnimEnt(entity, entity.animName, true);
        let delay = new AnimationHackCallback();
        delay.entity = entity;
        entity.animCallbackId = GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(delay, 2.0, false);
    }

    private cb func OnEntityAttached(event: ref<EntityLifecycleEvent>) {
        let entity = event.GetEntity() as GameObject;

        if IsDefined(entity) { // && entity.HasTag(n"CyberpunkMP.Puppet") {
            // LogChannel(n"DEBUG", s"[AppearanceSystem] OnEntityAssemble our muppet");
            if this.ApplyAppearance(entity) {
                ArrayPush(this.m_entities, entity);
            }

            // One marker per character, also when the entity is attached again (streaming).
            this.RemovePlayerMappin(entity.GetEntityID());

            let mappinData: MappinData;
            let mappinSystem: ref<MappinSystem> = GameInstance.GetMappinSystem(GetGameInstance());
            // The dynamic quest definition (also used for the player's own vehicle) is the one the world map shows;
            // the CPO_RemotePlayerMappinDefinition record only reaches the minimap.
            mappinData.mappinType = t"Mappins.QuestDynamicMappinDefinition";
            // barcode thing, ref base\\gameplay\\gui\\widgets\\mappins\\cyberspace\\cyberspace_object_mappin.inkwidget
            // mappinData.variant = gamedataMappinVariant.CyberspaceObject;
            // red phone thing, ref base\\gameplay\\gui\\widgets\\mappins\\interaction\\quick_hack_mappin.inkwidget
            // mappinData.variant = gamedataMappinVariant.PhoneCallVariant;
            // custom unused
            mappinData.variant = gamedataMappinVariant.CPO_RemotePlayerVariant;
            mappinData.active = true;
            // mappinData.visibleThroughWalls = true;
            // might need to adjust up a bit?
            let mappinId = mappinSystem.RegisterMappinWithObject(mappinData, entity, n"Nameplate");
            ArrayPush(this.m_playerMappinEntities, entity.GetEntityID());
            ArrayPush(this.m_playerMappins, mappinId);

            // this.SetMorphWeights(entity);
        } else {
            // LogChannel(n"DEBUG", s"[AppearanceSystem] not our muppet");
        }
    }

    // private func AddBodyParts(entity: ref<GameObject>, isMale: Bool) {
    //     let transactionSystem: ref<TransactionSystem> = GameInstance.GetTransactionSystem(entity.GetGame());
    //     if isMale {
    //         let head = ItemID.FromTDBID(t"Items.MuppetMaHead");
    //         transactionSystem.GiveItem(entity, head, 1);
    //         transactionSystem.AddItemToSlot(entity, EquipmentSystem.GetPlacementSlot(head), head);
    //     } else {
    //         let head = ItemID.FromTDBID(t"Items.MuppetWaHead");
    //         transactionSystem.GiveItem(entity, head, 1);
    //         transactionSystem.AddItemToSlot(entity, EquipmentSystem.GetPlacementSlot(head), head);
    //     }

    //     let arms = ItemID.FromTDBID(t"Items.MuppetArms");
    //     transactionSystem.GiveItem(entity, arms, 1);
    //     transactionSystem.AddItemToSlot(entity, EquipmentSystem.GetPlacementSlot(arms), arms);
    // }

    private func AddItems(entity: ref<GameObject>) {
        LogChannel(n"DEBUG", s"[AppearanceSystem] AddItems");
        let i: Int32;
        let transactionSystem: ref<TransactionSystem> = GameInstance.GetTransactionSystem(entity.GetGame());

        let equipment = this.GetEntityItems(entity.GetEntityID());
        i = 0;
        while i < ArraySize(equipment) {
            LogChannel(n"DEBUG", "Setting: " + TDBID.ToStringDEBUG(equipment[i]));
            let item = ItemID.FromTDBID(equipment[i]);
            let placementSlot = EquipmentSystem.GetPlacementSlot(item);

            // if EquipmentSystem.IsClothing(item) {
                if !transactionSystem.HasItem(entity, item) {
                    transactionSystem.GiveItem(entity, item, 1);
                    if !transactionSystem.HasItemInSlot(entity, placementSlot, item) {
                        transactionSystem.AddItemToSlot(entity, placementSlot, item);
                    }
                }
            // }

            // if EquipmentSystem.IsClothing(item) {
            //     transactionSystem.GivePreviewItemByItemID(entity, item);
            // } else {
            //     let itemData = transactionSystem.GetItemData(GetPlayer(entity.GetGame()), item);
            //     transactionSystem.GivePreviewItemByItemData(entity, itemData);
            // };
            // transactionSystem.AddItemToSlot(entity, placementSlot, transactionSystem.CreatePreviewItemID(item));

            i += 1;
        }
    }
}