#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "eztr_api.h"

#include "overlays/actors/ovl_En_Box/z_en_box.h"

#include "overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope.h"

#include "z64message.h"
#include "z64player.h"
//#include "assets/objects/gameplay_keep/gameplay_keep.h"

typedef void (*PlayerItemActionInitFunc)(PlayState*, Player*);

s32 Player_UpperAction_CarryActor(Player* this, PlayState* play);
s32 Player_UpperAction_ChangeHeldItem(Player* this, PlayState* play);

#define NewItemNum s16

typedef struct {
    /* 0x0 */ void (*drawFunc)(PlayState*, s16);
    /* 0x4 */ void* drawResources[8]; // Either display lists (Gfx*) or matrices (Mtx*)
} DrawItemTableEntry;                 // size = 0x24

typedef enum {
    IT_CUSTOM = 0,
    IT_MELEE_1H,
    IT_MELEE_2H,
    IT_USE,         //Immediately used items, like thrown Deku nuts
    IT_BOTTLED,
    IT_TRADE,
    IT_VIEWFINDER,  //Items that you 'look through', e.g. pictograph, telescope
    IT_PROJECTILE,  //Objects that 'shoot out of' something, uses first person + Z-targeting
    IT_EXPLOSIVE,   //Actually just any items that produce something 'held above your head'
    IT_SPELL,       //Magic with a casting animation
    IT_MASK,
    IT_TRANSFORMATION_MASK
} ItemType;

typedef enum {
    UI_OOT_MM,
    UI_OOT_ONLY,
    UI_MM_ONLY
} GamesUsedIn;

typedef enum {
    SA_AUTO_EMPTY = -1,     //for blank slots put there so the items can be collected later
    SA_AUTO_PREFILL = -2,   //for the slot to be filled with the item unconditionally
    SA_NONE = -3            //provides no slot assignment, mod should assign item using custom code
} SlotAssignment;

typedef struct {
    PlayerItemActionInitFunc initFunc; //Used to initialize the item action
    PlayerUpperActionFunc actionFunc;  //The item acton's update code
    u8 modelGroup;                      //Should be a valid PlayerModelGroup
    s16 slotAssignment;         //The inventory slot position number the item is to be assigned, otherwise SA_NONE gives no slot-
                                //for moders who want to deal with that themselves, while SA_AUTO_(EMPTY/PREFILL) generates a new one
} InventoryItemPerGameEntry;

typedef struct {
    char* itemName;             //Name the item will be refered to as internally, should be unique
    GamesUsedIn usedIn;         //Will this item be used in OoT, MM or both?
    TexturePtr icon;            //Pointer to RGBA32 Texture
    TexturePtr nameLabelEng;    //Pointer to IA8 Texture

    char* EZTR_KaleidoPopupText;        //Used in MM by any item assigned to a slot
    char* EZTR_GiveItemText;            //Used for givables
    InventoryItemPerGameEntry ocarinaFuncs;
    InventoryItemPerGameEntry mujuraFuncs;
    ItemType type;                      //optional, provides information to the integration code
    DrawItemTableEntry* drawEntryGI;    //Needed for givable/tradable items, otherwise optional
    s16 salePrice;                      //defaults to 0, meaning the item will not be sold, other numbers mean shops will accept it
    char* metadata;                     //optional, arbitrary null terminated data used to communicate with other mods
} CustomItemEntry;

typedef struct GetItemEntry {
    /* 0x0 */ u8 itemId;
    /* 0x1 */ u8 field; // various bit-packed data
    /* 0x2 */ s8 gid;   // defines the draw id and chest opening animation
    /* 0x3 */ u8 textId;
    /* 0x4 */ u16 objectId;
} GetItemEntry; // size = 0x6

#define IDLE_ANIM_NONE 0
#define CHEST_ANIM_SHORT 0
#define CHEST_ANIM_LONG 1

// TODO: consider what to do with the NONEs: cannot use a zero-argument macro like OoT since the text id is involved.
#define GET_ITEM(itemId, objectId, drawId, textId, field, chestAnim) \
    { itemId, field, (chestAnim != CHEST_ANIM_SHORT ? 1 : -1) * (drawId + 1), textId, objectId }

#define GIFIELD_GET_DROP_TYPE(field) ((field)&0x1F)
#define GIFIELD_20 (1 << 5)
#define GIFIELD_40 (1 << 6)
#define GIFIELD_NO_COLLECTIBLE (1 << 7)
/**
 * `flags` must be 0, GIFIELD_20, GIFIELD_40 or GIFIELD_NO_COLLECTIBLE (which can be or'ed together)
 * `dropType` must be either a value from the `Item00Type` enum or 0 if the `GIFIELD_NO_COLLECTIBLE` flag was used
 */
#define GIFIELD(flags, dropType) ((flags) | (dropType))
#define GI_START_TEXT 0xCE
#define GID_CUSTOM GID_4C

//This should be 0x50
#define LAST_REGULAR_ACTION_ITEM ITEM_SWORD_DEITY
#define NEW_ACTION_ITEMS (ITEM_CC+1)
#define ITEM_BOMBMINE NEW_ACTION_ITEMS
#define NUM_NEW_ITEMS 0x20
#define NEW_ACTION_NUMBERS PLAYER_IA_MAX
#define PLAYER_IA_BOMBMINE NEW_ACTION_NUMBERS
#define MESSAGE_ICON_NEW EZTR_ICON_NOTHING_51
#define MAX_REGULAR_SLOTS 0x30
#define NUM_NEW_INV_SLOTS NUM_NEW_ITEMS

extern s8 sItemItemActions[];
extern PlayerUpperActionFunc sItemActionUpdateFuncs[PLAYER_IA_MAX];
extern PlayerItemActionInitFunc sItemActionInitFuncs[PLAYER_IA_MAX];
extern u8 sActionModelGroups[PLAYER_IA_MAX];
extern s32 sPlayerUseHeldItem;
extern s32 sPlayerHeldItemButtonIsHeldDown;
extern PlayerAnimationHeader* D_8085BE84[PLAYER_ANIMGROUP_MAX][PLAYER_ANIMTYPE_MAX];
extern GetItemEntry sGetItemTable[GI_MAX - 1];
extern DrawItemTableEntry sDrawItemTable[];

u16 currentNewItemTotal = 0;
u16 currentNextFreeSlot = MAX_REGULAR_SLOTS;
s8 gNewItemActions[NUM_NEW_ITEMS] = {};
PlayerItemActionInitFunc gNewItemActionInitFuncs[NUM_NEW_ITEMS] = {};
PlayerUpperActionFunc gNewItemActionUpdateFuncs[NUM_NEW_ITEMS] = {};//Use Player_UpperAction_CarryActor
TexturePtr gNewItemIcons[NUM_NEW_ITEMS];
TexturePtr gNewItemNames[NUM_NEW_ITEMS];
u8 sNewActionModelGroups[NUM_NEW_ITEMS];
u8 sNewItemSlotAssignments[NUM_NEW_ITEMS];
s16 gNewInventoryItemSlots[NUM_NEW_ITEMS];
CustomItemEntry gCustomItemEntries[NUM_NEW_ITEMS];

RECOMP_PATCH PlayerModelGroup Player_ActionToModelGroup(Player* player, PlayerItemAction itemAction) {
    PlayerModelGroup modelGroup;
    if (itemAction < PLAYER_IA_MAX)
        modelGroup = sActionModelGroups[itemAction];
    else
        modelGroup = sNewActionModelGroups[itemAction-NEW_ACTION_NUMBERS];

    if ((modelGroup == PLAYER_MODELGROUP_ONE_HAND_SWORD) && Player_IsGoronOrDeku(player)) {
        return PLAYER_MODELGROUP_1;
    }
    return modelGroup;
}

extern s32 D_801F6B08;

extern Color_RGB8 D_801CFDEC[];
extern s16 D_801CFE04[];
extern s16 D_801CFE1C[];
extern s16 D_801CFE34[];
extern TexturePtr sStrayFairyIconTextures_code[];
extern Color_RGB8 sStrayFairyIconPrimColors_code[];
extern Color_RGB8 sStrayFairyIconEnvColors_code[];

extern s16 D_801CFF70[LANGUAGE_MAX];
extern s16 D_801CFF7C[LANGUAGE_MAX];
extern s16 D_801CFF88[LANGUAGE_MAX];

extern s16 D_801CFF94[];

extern TexturePtr gHeartFullTex;
extern TexturePtr gRupeeCounterIconTex;
extern TexturePtr gStrayFairyGlowingCircleIconTex;

s16 ItemExtension_ToNewItemRange(s16 itemID) {
    return itemID - NEW_ACTION_ITEMS;
}

s16 ItemExtension_FromItemRangeToItemID(s16 itemID) {
    return itemID + NEW_ACTION_ITEMS;
}

//Sets a special message item entry to denote that the text will display a new item icon
RECOMP_HOOK("Message_DecodeHeader") void Setup_D_801CFF94(PlayState* play) {
    D_801CFF94[MESSAGE_ICON_NEW] = NEW_ACTION_ITEMS;
}

RECOMP_HOOK("Message_DrawItemIcon") void Setup_DrawItemIcon(PlayState* play, Gfx** gfxP) {
     MessageContext* msgCtx = &play->msgCtx;
    if (msgCtx->itemId == NEW_ACTION_ITEMS) {
        msgCtx->itemId = ITEM_OCARINA_OF_TIME;
    }
}

static MessageContext* MyTempMsgCtx;
static u16 MyTempItemId;
static s16 MyTempArg2;

RECOMP_HOOK("Message_LoadItemIcon") void SetupMessage_LoadItemIcon(PlayState* play, u16 itemId, s16 arg2) {
     MyTempMsgCtx = &play->msgCtx;
     MyTempItemId = itemId;
     MyTempArg2 = arg2;
}

//Reloads the item icon using the currentTextId if we have one of the NEW_ACTION_ITEMS
RECOMP_HOOK_RETURN("Message_LoadItemIcon") void FinalizeMessage_LoadItemIcon() {
     MessageContext* msgCtx = MyTempMsgCtx;
    if (MyTempItemId == NEW_ACTION_ITEMS) {
        msgCtx->unk12010 = (msgCtx->unk11FF8 - D_801CFF70[gSaveContext.options.language]);
        msgCtx->unk12012 = (MyTempArg2 + 6);
        msgCtx->unk12014 = 0x20;
        if (msgCtx->currentTextId < 0x1700)
            memcpy(msgCtx->textboxSegment + 0x1000, gNewItemIcons[msgCtx->currentTextId-GI_START_TEXT], ICON_ITEM_TEX_SIZE);
        else
            memcpy(msgCtx->textboxSegment + 0x1000, gNewItemIcons[ItemExtension_ToNewItemRange(msgCtx->currentTextId-0x1700)], ICON_ITEM_TEX_SIZE);

    }
}

RECOMP_CALLBACK("*", recomp_on_init)
void on_init() {
}

void Core_Replace_Popup_Text(s16 newEntryNum, char* EZTR_text) {
    EZTR_Basic_ReplaceText(
        (0x1700+NEW_ACTION_ITEMS+newEntryNum),
        EZTR_STANDARD_TEXT_BOX_II,
        1,
        MESSAGE_ICON_NEW+newEntryNum,
        EZTR_NO_VALUE,
        EZTR_NO_VALUE,
        EZTR_NO_VALUE,
        true,
        EZTR_text,
        NULL
    );
}

void Core_Replace_Get_Text(s16 newEntryNum, char* EZTR_text) {
    EZTR_Basic_ReplaceText(
        (GI_START_TEXT+newEntryNum),
        EZTR_STANDARD_TEXT_BOX_II,
        1,
        MESSAGE_ICON_NEW+newEntryNum,
        EZTR_NO_VALUE,
        EZTR_NO_VALUE,
        EZTR_NO_VALUE,
        true,
        EZTR_text,
        NULL
    );
}

//Get Item globals
GetItemEntry gEntryGI = GET_ITEM(ITEM_BOMBCHUS_20, OBJECT_GI_BOMB_2, GID_BOMBCHU, 0x2E , GIFIELD(GIFIELD_20 | GIFIELD_NO_COLLECTIBLE, 0), CHEST_ANIM_LONG);
GetItemId gAlteredGI = GI_46; /*GI_MASK_CIRCUS_LEADER;*///GI_DEED_LAND;//

NewItemNum InitializeNewItemFromEntry(CustomItemEntry* entry) {
    if (currentNewItemTotal >= NUM_NEW_ITEMS)
        return -1;

    gCustomItemEntries[currentNewItemTotal] = *entry;
    gNewItemActions[currentNewItemTotal] = NEW_ACTION_NUMBERS+currentNewItemTotal;
    gNewItemActionInitFuncs[currentNewItemTotal] = entry->mujuraFuncs.initFunc;
    gNewItemActionUpdateFuncs[currentNewItemTotal] = entry->mujuraFuncs.actionFunc;
    gNewItemIcons[currentNewItemTotal] = entry->icon;
    gNewItemNames[currentNewItemTotal] = entry->nameLabelEng;
    sNewActionModelGroups[currentNewItemTotal] = entry->mujuraFuncs.modelGroup;
    if (entry->mujuraFuncs.slotAssignment >= 0)
        sNewItemSlotAssignments[currentNewItemTotal] = entry->mujuraFuncs.slotAssignment;
    else if (entry->mujuraFuncs.slotAssignment == SA_AUTO_EMPTY || entry->mujuraFuncs.slotAssignment == SA_AUTO_PREFILL)
        sNewItemSlotAssignments[currentNewItemTotal] = currentNextFreeSlot++;
    else
        sNewItemSlotAssignments[currentNewItemTotal] = SLOT_NONE;
    Core_Replace_Popup_Text(currentNewItemTotal, entry->EZTR_KaleidoPopupText);
    Core_Replace_Get_Text(currentNewItemTotal, entry->EZTR_GiveItemText);

    return currentNewItemTotal++;
}

RECOMP_DECLARE_EVENT(init_items_event());

EZTR_ON_INIT void ETZR_Item_Expansion_function() {
    for (s16 ii = 0; ii < NUM_NEW_INV_SLOTS; ii++) {
        gNewInventoryItemSlots[ii] = ITEM_NONE;
    }
    init_items_event();
}

RECOMP_HOOK("Player_InitCommon") void setup_inventory(Player* this, PlayState* play, FlexSkeletonHeader* skelHeader) {
    //////// REMOVE THESE ON RELEASE
    INV_CONTENT(ITEM_MOONS_TEAR) = ITEM_MOONS_TEAR;
    INV_CONTENT(ITEM_MASK_BREMEN) = ITEM_NONE;
    CLEAR_WEEKEVENTREG(WEEKEVENTREG_38_40);
    /////////
    //GetItemEntry entryGI = GET_ITEM(ITEM_BOMBCHUS_20, OBJECT_GI_BOMB_2, GID_BOMBCHU, 0x2E, GIFIELD(GIFIELD_40 | GIFIELD_NO_COLLECTIBLE, 0), CHEST_ANIM_SHORT);
    recomp_printf("ItemTable- %d: %d\n", gAlteredGI-1,sGetItemTable[gAlteredGI-1].itemId);
    // sGetItemTable[GI_DEED_LAND+1] = gEntryGI;
    // sGetItemTable[GI_DEED_LAND] = gEntryGI;
    // sGetItemTable[GI_DEED_LAND-1] = gEntryGI;
    // recomp_printf("ItemTable- %d: %d\n", GI_DEED_LAND-1,sGetItemTable[GI_DEED_LAND-1].itemId);
    for (s16 ii = 0; ii < currentNewItemTotal; ii++) {
        if (sNewItemSlotAssignments[ii] >= 0 && sNewItemSlotAssignments[ii] < SLOT_NONE) {
            if (sNewItemSlotAssignments[ii] < MAX_REGULAR_SLOTS)
                gSaveContext.save.saveInfo.inventory.items[sNewItemSlotAssignments[ii]] = ItemExtension_FromItemRangeToItemID(ii);
            else if (gCustomItemEntries[ii].mujuraFuncs.slotAssignment == SA_AUTO_PREFILL)
                gNewInventoryItemSlots[sNewItemSlotAssignments[ii]-MAX_REGULAR_SLOTS] = ItemExtension_FromItemRangeToItemID(ii);

            //SET_CUR_FORM_BTN_ITEM(EQUIP_SLOT_C_LEFT, ItemExtension_FromItemRangeToItemID(sBombmineIN));
            //SET_CUR_FORM_BTN_SLOT(EQUIP_SLOT_C_LEFT, sNewItemSlotAssignments[sBombmineIN]);
            //Interface_LoadItemIconImpl(play, EQUIP_SLOT_C_LEFT);
        }
    }
}

s32 ItemExtension_OfferExtendedGetItem(Actor* actor, PlayState* play, s16 getItemIdEx, f32 xzRange, f32 yRange) {
    gEntryGI.itemId = ItemExtension_FromItemRangeToItemID(getItemIdEx);
    gEntryGI.textId = GI_START_TEXT+getItemIdEx;
    gEntryGI.objectId = OBJECT_UNSET_0;
    gEntryGI.gid = GID_CUSTOM+1;
    sDrawItemTable[GID_CUSTOM] = *gCustomItemEntries[getItemIdEx].drawEntryGI;
    //sDrawItemTable[GID_CUSTOM].drawResources[0] = gGiBugContainerContentsDL;
    //sDrawItemTable[GID_CUSTOM].drawResources[1] = gGiBugContainerGlassDL;
    return Actor_OfferGetItem(actor, play, gAlteredGI, xzRange, yRange);
}

s32 ItemExtension_OfferExtendedGetItemFar(Actor* actor, PlayState* play, s16 getItemIdEx) {
    return ItemExtension_OfferExtendedGetItem(actor, play, getItemIdEx, 9999.9f, 9999.9f);
}

s32 ItemExtension_OfferExtendedGetItemUnconditional(Actor* actor, PlayState* play, s16 getItemIdEx) {
    GetItemId getItemId = gAlteredGI;
    gEntryGI.itemId = ItemExtension_FromItemRangeToItemID(getItemIdEx);
    gEntryGI.textId = GI_START_TEXT+getItemIdEx;
    gEntryGI.objectId = OBJECT_UNSET_0;
    gEntryGI.gid = GID_CUSTOM+1;
    sDrawItemTable[GID_CUSTOM] = *gCustomItemEntries[getItemIdEx].drawEntryGI;
    //sDrawItemTable[GID_CUSTOM].drawResources[0] = gGiBugContainerContentsDL;
    //sDrawItemTable[GID_CUSTOM].drawResources[1] = gGiBugContainerGlassDL;
    Player* player = GET_PLAYER(play);

    if (!(player->stateFlags1 &
          (PLAYER_STATE1_DEAD | PLAYER_STATE1_CHARGING_SPIN_ATTACK | PLAYER_STATE1_2000 | PLAYER_STATE1_4000 |
           PLAYER_STATE1_40000 | PLAYER_STATE1_80000 | PLAYER_STATE1_100000 | PLAYER_STATE1_200000)) &&
        (Player_GetExplosiveHeld(player) <= PLAYER_EXPLOSIVE_NONE)) {
        s16 yawDiff = actor->yawTowardsPlayer - player->actor.shape.rot.y;
        s32 absYawDiff = ABS_ALT(yawDiff);

        if ((getItemId != GI_NONE) || (player->getItemDirection < absYawDiff)) {
            player->getItemId = getItemId;
            player->interactRangeActor = actor;
            player->getItemDirection = absYawDiff;

            if ((getItemId > GI_NONE) && (getItemId < GI_MAX)) {
                CutsceneManager_Queue(play->playerCsIds[PLAYER_CS_ID_ITEM_GET]);
            }

            return true;
        }
    }

    return false;
}

RECOMP_HOOK("func_8082ECE0")
void Setup_func_8082ECE0(Player* this) {
    // GetItemEntry entryGI = GET_ITEM(ITEM_BOMBCHUS_20, OBJECT_GI_BOMB_2, GID_BOMBCHU, 0x2E, GIFIELD(GIFIELD_40 | GIFIELD_NO_COLLECTIBLE, 0), CHEST_ANIM_SHORT);
    sGetItemTable[gAlteredGI-1] = gEntryGI;
}

RECOMP_HOOK("Player_ActionHandler_2")
void Setup_Player_ActionHandler_2(Player* this) {
    // GetItemEntry entryGI = GET_ITEM(ITEM_BOMBCHUS_20, OBJECT_GI_BOMB_2, GID_BOMBCHU, 0x2E, GIFIELD(GIFIELD_40 | GIFIELD_NO_COLLECTIBLE, 0), CHEST_ANIM_SHORT);
    sGetItemTable[gAlteredGI-1] = gEntryGI;
}

RECOMP_HOOK("func_808482E0")
void Setup_func_808482E0(PlayState* play, Player* this) {
    // GetItemEntry entryGI = GET_ITEM(ITEM_BOMBCHUS_20, OBJECT_GI_BOMB_2, GID_BOMBCHU, 0x2E, GIFIELD(GIFIELD_40 | GIFIELD_NO_COLLECTIBLE, 0), CHEST_ANIM_SHORT);
    sGetItemTable[gAlteredGI-1] = gEntryGI;
}

RECOMP_HOOK("Player_Action_ExchangeItem")
void Setup_Player_Action_ExchangeItem(Player* this, PlayState* play) {
    // GetItemEntry entryGI = GET_ITEM(ITEM_BOMBCHUS_20, OBJECT_GI_BOMB_2, GID_BOMBCHU, 0x2E, GIFIELD(GIFIELD_40 | GIFIELD_NO_COLLECTIBLE, 0), CHEST_ANIM_SHORT);
    sGetItemTable[gAlteredGI-1] = gEntryGI;
}

#define SPECIAL_ITEM_CHEST_VALUE 0xFB
#define SPECIAL_ITEM_CHEST_PARAM ((((u16)SPECIAL_ITEM_CHEST_VALUE))<<7)

s8 gUnk_1F3 = 0;
EnBox* gBox;

RECOMP_HOOK("EnBox_Init")
void Setup_EnBox_Init(Actor* thisx, PlayState* play) {
    EnBox* box = ((EnBox*)thisx);
    gBox = box;
    recomp_printf("EnBox_Init- %d: %d\n", thisx->world.rot.z,((thisx->world.rot.z>>7) & 0xFF));
    if (((thisx->world.rot.z>>7) & 0xFF) == SPECIAL_ITEM_CHEST_VALUE) {
        box->unk_1F3 = ENBOX_GET_ITEM(thisx)+1;
        gUnk_1F3 = ENBOX_GET_ITEM(thisx)+1;;
        thisx->params &= ~(0x7F << 5);
        thisx->params |= (gAlteredGI << 5);
        thisx->world.rot.z &= ~(0xFF << 7);
        recomp_printf("EnBox_Init2- %d\n", box->unk_1F3);
    } else {
        gUnk_1F3 = 0;
    }
}

RECOMP_HOOK_RETURN("EnBox_Init")
void Finalize_EnBox_Init() {
    gBox->unk_1F3 = gUnk_1F3;
}

RECOMP_HOOK("EnBox_WaitOpen")
void Setup_EnBox_WaitOpen(EnBox* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (this->unk_1F3) {
        Vec3f offset;
        Actor_WorldToActorCoords(&this->dyna.actor, &offset, &player->actor.world.pos);
        if ((offset.z > -50.0f) && (offset.z < 0.0f) && (fabsf(offset.y) < 10.0f) && (fabsf(offset.x) < 20.0f) &&
                Player_IsFacingActor(&this->dyna.actor, 0x3000, play)) {

            recomp_printf("EnBox_WaitOpen2- %d\n", this->unk_1F3);
            gEntryGI.itemId = ItemExtension_FromItemRangeToItemID(this->unk_1F3-1);
            gEntryGI.textId = GI_START_TEXT+this->unk_1F3-1;
            gEntryGI.objectId = OBJECT_UNSET_0;
            gEntryGI.gid = GID_CUSTOM;
            sDrawItemTable[GID_CUSTOM-1] = *gCustomItemEntries[this->unk_1F3-1].drawEntryGI;
        }
    }
}

//////// REMOVE THESE ON RELEASE
// #include "overlays/actors/ovl_En_Guruguru/z_en_guruguru.h"
// #include "overlays/actors/ovl_En_Sellnuts/z_en_sellnuts.h"
// extern u16 textIDs[];
// void func_80BC7520(EnGuruguru* this, PlayState* play);
// void func_80ADBCE4(EnSellnuts* this, PlayState* play);
//
// RECOMP_HOOK("EnSellnuts_Init")
// void Setup_EnSellnuts_Init(Actor* thisx, PlayState* play){
//     EnSellnuts* this = (EnSellnuts*)thisx;
//     Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOX, this->actor.world.pos.x+150, this->actor.world.pos.y,
//                 this->actor.world.pos.z, 0, this->actor.shape.rot.y, SPECIAL_ITEM_CHEST_PARAM | 0x7F,
//                 ENBOX_PARAMS(ENBOX_TYPE_BIG, 0, 0x12));
// }
//
// RECOMP_PATCH
// void func_80ADBBEC(EnSellnuts* this, PlayState* play) {
//     if (Actor_HasParent(&this->actor, play)) {
//         this->actor.parent = NULL;
//         SET_WEEKEVENTREG(WEEKEVENTREG_RECEIVED_LAND_TITLE_DEED);
//         this->actionFunc = func_80ADBCE4;
//     } else {
//         ItemExtension_OfferExtendedGetItemUnconditional(&this->actor, play, sBombmineIN);
//     }
// }
//
// RECOMP_PATCH
// void func_80BC7440(EnGuruguru* this, PlayState* play) {
//     SkelAnime_Update(&this->skelAnime);
//     if (Actor_HasParent(&this->actor, play)) {
//         this->actor.parent = NULL;
//         this->textIdIndex++;
//         this->actor.textId = textIDs[this->textIdIndex];
//         Audio_MuteSeqPlayerBgmSub(true);
//         Actor_OfferTalkExchange(&this->actor, play, 400.0f, 400.0f, PLAYER_IA_MINUS1);
//         this->unk268 = 0;
//         SET_WEEKEVENTREG(WEEKEVENTREG_38_40);
//         this->actionFunc = func_80BC7520;
//     } else {
//         ItemExtension_OfferExtendedGetItemFar(&this->actor, play, sBombmineIN);
//     }
// }
/////////

extern s16 sExtraItemBases[];
extern s16 sAmmoRefillCounts[]; // Sticks, nuts, bombs
extern s16 sArrowRefillCounts[];
extern s16 sBombchuRefillCounts[];
extern s16 sRupeeRefillCounts[];

RECOMP_PATCH
u8 Item_CheckObtainability(u8 item) {
    if (ItemExtension_ToNewItemRange(item) >= 0)
        return ITEM_NONE; //gNewInventoryItemSlots[ItemExtension_ToNewItemRange(item)];

    return Item_CheckObtainabilityImpl(item);
}

RECOMP_PATCH
u8 Item_Give(PlayState* play, u8 item) {
    Player* player = GET_PLAYER(play);
    u8 i;
    u8 temp;
    u8 slot;

    if (ItemExtension_ToNewItemRange(item) >= 0) {
        slot = sNewItemSlotAssignments[ItemExtension_ToNewItemRange(item)];
        if (slot != SLOT_NONE) {
            if (slot < MAX_REGULAR_SLOTS)
                gSaveContext.save.saveInfo.inventory.items[slot] = item;
            else
                gNewInventoryItemSlots[slot-MAX_REGULAR_SLOTS] = item;
        }
        return ITEM_NONE;
    }

    slot = SLOT(item);
    if (item >= ITEM_DEKU_STICKS_5) {
        slot = SLOT(sExtraItemBases[item - ITEM_DEKU_STICKS_5]);
    }

    if (item == ITEM_SKULL_TOKEN) {
        //! @bug: Sets QUEST_QUIVER instead of QUEST_SKULL_TOKEN
        // Setting `QUEST_SKULL_TOKEN` will result in misplaced digits on the pause menu - Quest Status page.
        SET_QUEST_ITEM(item - ITEM_SKULL_TOKEN + QUEST_QUIVER);
        Inventory_IncrementSkullTokenCount(play->sceneId);
        return ITEM_NONE;

    } else if (item == ITEM_TINGLE_MAP) {
        return ITEM_NONE;

    } else if (item == ITEM_BOMBERS_NOTEBOOK) {
        SET_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK);
        return ITEM_NONE;

    } else if ((item == ITEM_HEART_PIECE_2) || (item == ITEM_HEART_PIECE)) {
        INCREMENT_QUEST_HEART_PIECE_COUNT;
        if (EQ_MAX_QUEST_HEART_PIECE_COUNT) {
            RESET_HEART_PIECE_COUNT;
            gSaveContext.save.saveInfo.playerData.healthCapacity += 0x10;
            gSaveContext.save.saveInfo.playerData.health += 0x10;
        }
        return ITEM_NONE;

    } else if (item == ITEM_HEART_CONTAINER) {
        gSaveContext.save.saveInfo.playerData.healthCapacity += 0x10;
        gSaveContext.save.saveInfo.playerData.health += 0x10;
        return ITEM_NONE;

    } else if ((item >= ITEM_SONG_SONATA) && (item <= ITEM_SONG_LULLABY_INTRO)) {
        SET_QUEST_ITEM(item - ITEM_SONG_SONATA + QUEST_SONG_SONATA);
        return ITEM_NONE;

    } else if ((item >= ITEM_SWORD_KOKIRI) && (item <= ITEM_SWORD_GILDED)) {
        SET_EQUIP_VALUE(EQUIP_TYPE_SWORD, item - ITEM_SWORD_KOKIRI + EQUIP_VALUE_SWORD_KOKIRI);
        CUR_FORM_EQUIP(EQUIP_SLOT_B) = item;
        Interface_LoadItemIconImpl(play, EQUIP_SLOT_B);
        if (item == ITEM_SWORD_RAZOR) {
            gSaveContext.save.saveInfo.playerData.swordHealth = 100;
        }
        return ITEM_NONE;

    } else if ((item >= ITEM_SHIELD_HERO) && (item <= ITEM_SHIELD_MIRROR)) {
        if (GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) != (u16)(item - ITEM_SHIELD_HERO + EQUIP_VALUE_SHIELD_HERO)) {
            SET_EQUIP_VALUE(EQUIP_TYPE_SHIELD, item - ITEM_SHIELD_HERO + EQUIP_VALUE_SHIELD_HERO);
            Player_SetEquipmentData(play, player);
            return ITEM_NONE;
        }
        return item;

    } else if ((item == ITEM_KEY_BOSS) || (item == ITEM_COMPASS) || (item == ITEM_DUNGEON_MAP)) {
        SET_DUNGEON_ITEM(item - ITEM_KEY_BOSS, gSaveContext.mapIndex);
        return ITEM_NONE;

    } else if (item == ITEM_KEY_SMALL) {
        if (DUNGEON_KEY_COUNT(gSaveContext.mapIndex) < 0) {
            DUNGEON_KEY_COUNT(gSaveContext.mapIndex) = 1;
            return ITEM_NONE;
        } else {
            DUNGEON_KEY_COUNT(gSaveContext.mapIndex)++;
            return ITEM_NONE;
        }

    } else if ((item == ITEM_QUIVER_30) || (item == ITEM_BOW)) {
        if (CUR_UPG_VALUE(UPG_QUIVER) == 0) {
            Inventory_ChangeUpgrade(UPG_QUIVER, 1);
            INV_CONTENT(ITEM_BOW) = ITEM_BOW;
            AMMO(ITEM_BOW) = CAPACITY(UPG_QUIVER, 1);
            return ITEM_NONE;
        } else {
            AMMO(ITEM_BOW)++;
            if (AMMO(ITEM_BOW) > (s8)CUR_CAPACITY(UPG_QUIVER)) {
                AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
            }
        }

    } else if (item == ITEM_QUIVER_40) {
        Inventory_ChangeUpgrade(UPG_QUIVER, 2);
        INV_CONTENT(ITEM_BOW) = ITEM_BOW;
        AMMO(ITEM_BOW) = CAPACITY(UPG_QUIVER, 2);
        return ITEM_NONE;

    } else if (item == ITEM_QUIVER_50) {
        Inventory_ChangeUpgrade(UPG_QUIVER, 3);
        INV_CONTENT(ITEM_BOW) = ITEM_BOW;
        AMMO(ITEM_BOW) = CAPACITY(UPG_QUIVER, 3);
        return ITEM_NONE;

    } else if (item == ITEM_BOMB_BAG_20) {
        if (CUR_UPG_VALUE(UPG_BOMB_BAG) == 0) {
            Inventory_ChangeUpgrade(UPG_BOMB_BAG, 1);
            INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
            AMMO(ITEM_BOMB) = CAPACITY(UPG_BOMB_BAG, 1);
            return ITEM_NONE;

        } else {
            AMMO(ITEM_BOMB)++;
            if (AMMO(ITEM_BOMB) > CUR_CAPACITY(UPG_BOMB_BAG)) {
                AMMO(ITEM_BOMB) = CUR_CAPACITY(UPG_BOMB_BAG);
            }
        }

    } else if (item == ITEM_BOMB_BAG_30) {
        Inventory_ChangeUpgrade(UPG_BOMB_BAG, 2);
        INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
        AMMO(ITEM_BOMB) = CAPACITY(UPG_BOMB_BAG, 2);
        return ITEM_NONE;

    } else if (item == ITEM_BOMB_BAG_40) {
        Inventory_ChangeUpgrade(UPG_BOMB_BAG, 3);
        INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
        AMMO(ITEM_BOMB) = CAPACITY(UPG_BOMB_BAG, 3);
        return ITEM_NONE;

    } else if (item == ITEM_WALLET_ADULT) {
        Inventory_ChangeUpgrade(UPG_WALLET, 1);
        return ITEM_NONE;

    } else if (item == ITEM_WALLET_GIANT) {
        Inventory_ChangeUpgrade(UPG_WALLET, 2);
        return ITEM_NONE;

    } else if (item == ITEM_DEKU_STICK_UPGRADE_20) {
        if (INV_CONTENT(ITEM_DEKU_STICK) != ITEM_DEKU_STICK) {
            INV_CONTENT(ITEM_DEKU_STICK) = ITEM_DEKU_STICK;
        }
        Inventory_ChangeUpgrade(UPG_DEKU_STICKS, 2);
        AMMO(ITEM_DEKU_STICK) = CAPACITY(UPG_DEKU_STICKS, 2);
        return ITEM_NONE;

    } else if (item == ITEM_DEKU_STICK_UPGRADE_30) {
        if (INV_CONTENT(ITEM_DEKU_STICK) != ITEM_DEKU_STICK) {
            INV_CONTENT(ITEM_DEKU_STICK) = ITEM_DEKU_STICK;
        }
        Inventory_ChangeUpgrade(UPG_DEKU_STICKS, 3);
        AMMO(ITEM_DEKU_STICK) = CAPACITY(UPG_DEKU_STICKS, 3);
        return ITEM_NONE;

    } else if (item == ITEM_DEKU_NUT_UPGRADE_30) {
        if (INV_CONTENT(ITEM_DEKU_NUT) != ITEM_DEKU_NUT) {
            INV_CONTENT(ITEM_DEKU_NUT) = ITEM_DEKU_NUT;
        }
        Inventory_ChangeUpgrade(UPG_DEKU_NUTS, 2);
        AMMO(ITEM_DEKU_NUT) = CAPACITY(UPG_DEKU_NUTS, 2);
        return ITEM_NONE;

    } else if (item == ITEM_DEKU_NUT_UPGRADE_40) {
        if (INV_CONTENT(ITEM_DEKU_NUT) != ITEM_DEKU_NUT) {
            INV_CONTENT(ITEM_DEKU_NUT) = ITEM_DEKU_NUT;
        }
        Inventory_ChangeUpgrade(UPG_DEKU_NUTS, 3);
        AMMO(ITEM_DEKU_NUT) = CAPACITY(UPG_DEKU_NUTS, 3);
        return ITEM_NONE;

    } else if (item == ITEM_DEKU_STICK) {
        if (INV_CONTENT(ITEM_DEKU_STICK) != ITEM_DEKU_STICK) {
            Inventory_ChangeUpgrade(UPG_DEKU_STICKS, 1);
            AMMO(ITEM_DEKU_STICK) = 1;
        } else {
            AMMO(ITEM_DEKU_STICK)++;
            if (AMMO(ITEM_DEKU_STICK) > CUR_CAPACITY(UPG_DEKU_STICKS)) {
                AMMO(ITEM_DEKU_STICK) = CUR_CAPACITY(UPG_DEKU_STICKS);
            }
        }

    } else if ((item == ITEM_DEKU_STICKS_5) || (item == ITEM_DEKU_STICKS_10)) {
        if (INV_CONTENT(ITEM_DEKU_STICK) != ITEM_DEKU_STICK) {
            Inventory_ChangeUpgrade(UPG_DEKU_STICKS, 1);
            AMMO(ITEM_DEKU_STICK) = sAmmoRefillCounts[item - ITEM_DEKU_STICKS_5];
        } else {
            AMMO(ITEM_DEKU_STICK) += sAmmoRefillCounts[item - ITEM_DEKU_STICKS_5];
            if (AMMO(ITEM_DEKU_STICK) > CUR_CAPACITY(UPG_DEKU_STICKS)) {
                AMMO(ITEM_DEKU_STICK) = CUR_CAPACITY(UPG_DEKU_STICKS);
            }
        }

        item = ITEM_DEKU_STICK;

    } else if (item == ITEM_DEKU_NUT) {
        if (INV_CONTENT(ITEM_DEKU_NUT) != ITEM_DEKU_NUT) {
            Inventory_ChangeUpgrade(UPG_DEKU_NUTS, 1);
            AMMO(ITEM_DEKU_NUT) = 1;
        } else {
            AMMO(ITEM_DEKU_NUT)++;
            if (AMMO(ITEM_DEKU_NUT) > CUR_CAPACITY(UPG_DEKU_NUTS)) {
                AMMO(ITEM_DEKU_NUT) = CUR_CAPACITY(UPG_DEKU_NUTS);
            }
        }

    } else if ((item == ITEM_DEKU_NUTS_5) || (item == ITEM_DEKU_NUTS_10)) {
        if (INV_CONTENT(ITEM_DEKU_NUT) != ITEM_DEKU_NUT) {
            Inventory_ChangeUpgrade(UPG_DEKU_NUTS, 1);
            AMMO(ITEM_DEKU_NUT) += sAmmoRefillCounts[item - ITEM_DEKU_NUTS_5];
        } else {
            AMMO(ITEM_DEKU_NUT) += sAmmoRefillCounts[item - ITEM_DEKU_NUTS_5];
            if (AMMO(ITEM_DEKU_NUT) > CUR_CAPACITY(UPG_DEKU_NUTS)) {
                AMMO(ITEM_DEKU_NUT) = CUR_CAPACITY(UPG_DEKU_NUTS);
            }
        }
        item = ITEM_DEKU_NUT;

    } else if (item == ITEM_POWDER_KEG) {
        if (INV_CONTENT(ITEM_POWDER_KEG) != ITEM_POWDER_KEG) {
            INV_CONTENT(ITEM_POWDER_KEG) = ITEM_POWDER_KEG;
        }

        AMMO(ITEM_POWDER_KEG) = 1;
        return ITEM_NONE;

    } else if (item == ITEM_BOMB) {
        if ((AMMO(ITEM_BOMB) += 1) > CUR_CAPACITY(UPG_BOMB_BAG)) {
            AMMO(ITEM_BOMB) = CUR_CAPACITY(UPG_BOMB_BAG);
        }
        return ITEM_NONE;

    } else if ((item >= ITEM_BOMBS_5) && (item <= ITEM_BOMBS_30)) {
        if (gSaveContext.save.saveInfo.inventory.items[SLOT_BOMB] != ITEM_BOMB) {
            INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
            AMMO(ITEM_BOMB) += sAmmoRefillCounts[item - ITEM_BOMBS_5];
            return ITEM_NONE;
        }

        if ((AMMO(ITEM_BOMB) += sAmmoRefillCounts[item - ITEM_BOMBS_5]) > CUR_CAPACITY(UPG_BOMB_BAG)) {
            AMMO(ITEM_BOMB) = CUR_CAPACITY(UPG_BOMB_BAG);
        }
        return ITEM_NONE;

    } else if (item == ITEM_BOMBCHU) {
        if (INV_CONTENT(ITEM_BOMBCHU) != ITEM_BOMBCHU) {
            INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
            AMMO(ITEM_BOMBCHU) = 10;
            return ITEM_NONE;
        }
        if ((AMMO(ITEM_BOMBCHU) += 10) > CUR_CAPACITY(UPG_BOMB_BAG)) {
            AMMO(ITEM_BOMBCHU) = CUR_CAPACITY(UPG_BOMB_BAG);
        }
        return ITEM_NONE;

    } else if ((item >= ITEM_BOMBCHUS_20) && (item <= ITEM_BOMBCHUS_5)) {
        if (gSaveContext.save.saveInfo.inventory.items[SLOT_BOMBCHU] != ITEM_BOMBCHU) {
            INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
            AMMO(ITEM_BOMBCHU) += sBombchuRefillCounts[item - ITEM_BOMBCHUS_20];

            if (AMMO(ITEM_BOMBCHU) > CUR_CAPACITY(UPG_BOMB_BAG)) {
                AMMO(ITEM_BOMBCHU) = CUR_CAPACITY(UPG_BOMB_BAG);
            }
            return ITEM_NONE;
        }

        if ((AMMO(ITEM_BOMBCHU) += sBombchuRefillCounts[item - ITEM_BOMBCHUS_20]) > CUR_CAPACITY(UPG_BOMB_BAG)) {
            AMMO(ITEM_BOMBCHU) = CUR_CAPACITY(UPG_BOMB_BAG);
        }
        return ITEM_NONE;

    } else if ((item >= ITEM_ARROWS_10) && (item <= ITEM_ARROWS_50)) {
        AMMO(ITEM_BOW) += sArrowRefillCounts[item - ITEM_ARROWS_10];

        if ((AMMO(ITEM_BOW) >= CUR_CAPACITY(UPG_QUIVER)) || (AMMO(ITEM_BOW) < 0)) {
            AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
        }
        return ITEM_BOW;

    } else if (item == ITEM_OCARINA_OF_TIME) {
        INV_CONTENT(ITEM_OCARINA_OF_TIME) = ITEM_OCARINA_OF_TIME;
        return ITEM_NONE;

    } else if (item == ITEM_MAGIC_BEANS) {
        if (INV_CONTENT(ITEM_MAGIC_BEANS) == ITEM_NONE) {
            INV_CONTENT(item) = item;
            AMMO(ITEM_MAGIC_BEANS) = 1;
        } else if (AMMO(ITEM_MAGIC_BEANS) < 20) {
            AMMO(ITEM_MAGIC_BEANS)++;
        } else {
            AMMO(ITEM_MAGIC_BEANS) = 20;
        }
        return ITEM_NONE;

    } else if ((item >= ITEM_REMAINS_ODOLWA) && (item <= ITEM_REMAINS_TWINMOLD)) {
        SET_QUEST_ITEM(item - ITEM_REMAINS_ODOLWA + QUEST_REMAINS_ODOLWA);
        return ITEM_NONE;

    } else if (item == ITEM_RECOVERY_HEART) {
        Health_ChangeBy(play, 0x10);
        return item;

    } else if (item == ITEM_MAGIC_JAR_SMALL) {
        Magic_Add(play, MAGIC_NORMAL_METER / 2);
        if (!CHECK_WEEKEVENTREG(WEEKEVENTREG_12_80)) {
            SET_WEEKEVENTREG(WEEKEVENTREG_12_80);
            return ITEM_NONE;
        }
        return item;

    } else if (item == ITEM_MAGIC_JAR_BIG) {
        Magic_Add(play, MAGIC_NORMAL_METER);
        if (!CHECK_WEEKEVENTREG(WEEKEVENTREG_12_80)) {
            SET_WEEKEVENTREG(WEEKEVENTREG_12_80);
            return ITEM_NONE;
        }
        return item;

    } else if ((item >= ITEM_RUPEE_GREEN) && (item <= ITEM_RUPEE_HUGE)) {
        Rupees_ChangeBy(sRupeeRefillCounts[item - ITEM_RUPEE_GREEN]);
        return ITEM_NONE;

    } else if (item == ITEM_LONGSHOT) {
        slot = SLOT(item);

        for (i = BOTTLE_FIRST; i < BOTTLE_MAX; i++) {
            if (gSaveContext.save.saveInfo.inventory.items[slot + i] == ITEM_NONE) {
                gSaveContext.save.saveInfo.inventory.items[slot + i] = ITEM_POTION_RED;
                return ITEM_NONE;
            }
        }
        return item;

    } else if ((item == ITEM_MILK_BOTTLE) || (item == ITEM_POE) || (item == ITEM_GOLD_DUST) || (item == ITEM_CHATEAU) ||
               (item == ITEM_HYLIAN_LOACH)) {
        slot = SLOT(item);

        for (i = BOTTLE_FIRST; i < BOTTLE_MAX; i++) {
            if (gSaveContext.save.saveInfo.inventory.items[slot + i] == ITEM_NONE) {
                gSaveContext.save.saveInfo.inventory.items[slot + i] = item;
                return ITEM_NONE;
            }
        }
        return item;

    } else if (item == ITEM_BOTTLE) {
        slot = SLOT(item);

        for (i = BOTTLE_FIRST; i < BOTTLE_MAX; i++) {
            if (gSaveContext.save.saveInfo.inventory.items[slot + i] == ITEM_NONE) {
                gSaveContext.save.saveInfo.inventory.items[slot + i] = item;
                return ITEM_NONE;
            }
        }
        return item;

    } else if (((item >= ITEM_POTION_RED) && (item <= ITEM_OBABA_DRINK)) || (item == ITEM_CHATEAU_2) ||
               (item == ITEM_MILK) || (item == ITEM_GOLD_DUST_2) || (item == ITEM_HYLIAN_LOACH_2) ||
               (item == ITEM_SEAHORSE_CAUGHT)) {
        slot = SLOT(item);

        if ((item != ITEM_MILK_BOTTLE) && (item != ITEM_MILK_HALF)) {
            if (item == ITEM_CHATEAU_2) {
                item = ITEM_CHATEAU;

            } else if (item == ITEM_MILK) {
                item = ITEM_MILK_BOTTLE;

            } else if (item == ITEM_GOLD_DUST_2) {
                item = ITEM_GOLD_DUST;

            } else if (item == ITEM_HYLIAN_LOACH_2) {
                item = ITEM_HYLIAN_LOACH;

            } else if (item == ITEM_SEAHORSE_CAUGHT) {
                item = ITEM_SEAHORSE;
            }
            slot = SLOT(item);

            for (i = BOTTLE_FIRST; i < BOTTLE_MAX; i++) {
                if (gSaveContext.save.saveInfo.inventory.items[slot + i] == ITEM_BOTTLE) {
                    if (item == ITEM_HOT_SPRING_WATER) {
                        Interface_StartBottleTimer(60, i);
                    }

                    if ((slot + i) == C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT)) {
                        BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) = item;
                        Interface_LoadItemIconImpl(play, EQUIP_SLOT_C_LEFT);
                        gSaveContext.buttonStatus[EQUIP_SLOT_C_LEFT] = BTN_ENABLED;
                    } else if ((slot + i) == C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN)) {
                        BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) = item;
                        Interface_LoadItemIconImpl(play, EQUIP_SLOT_C_DOWN);
                        gSaveContext.buttonStatus[EQUIP_SLOT_C_DOWN] = BTN_ENABLED;
                    } else if ((slot + i) == C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT)) {
                        BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) = item;
                        Interface_LoadItemIconImpl(play, EQUIP_SLOT_C_RIGHT);
                        gSaveContext.buttonStatus[EQUIP_SLOT_C_RIGHT] = BTN_ENABLED;
                    }

                    gSaveContext.save.saveInfo.inventory.items[slot + i] = item;
                    return ITEM_NONE;
                }
            }
        } else {
            for (i = BOTTLE_FIRST; i < BOTTLE_MAX; i++) {
                if (gSaveContext.save.saveInfo.inventory.items[slot + i] == ITEM_NONE) {
                    gSaveContext.save.saveInfo.inventory.items[slot + i] = item;
                    return ITEM_NONE;
                }
            }
        }

    } else if ((item >= ITEM_MOONS_TEAR) && (item <= ITEM_MASK_GIANT)) {
        temp = INV_CONTENT(item);
        INV_CONTENT(item) = item;
        if ((item >= ITEM_MOONS_TEAR) && (item <= ITEM_PENDANT_OF_MEMORIES) && (temp != ITEM_NONE)) {
            for (i = EQUIP_SLOT_C_LEFT; i <= EQUIP_SLOT_C_RIGHT; i++) {
                if (temp == GET_CUR_FORM_BTN_ITEM(i)) {
                    SET_CUR_FORM_BTN_ITEM(i, item);
                    Interface_LoadItemIconImpl(play, i);
                    return ITEM_NONE;
                }
            }
        }
        return ITEM_NONE;
    }

    temp = gSaveContext.save.saveInfo.inventory.items[slot];
    INV_CONTENT(item) = item;
    return temp;
}

PlayerItemAction getUpdatedItemAction(ItemId item) {
    recomp_printf("getUpdatedItemId: %d\n", item);
    if (item < NEW_ACTION_ITEMS) {
        return sItemItemActions[item];
    } else  {
        recomp_printf("getUpdatedItemAction: %d\n", gNewItemActions[ItemExtension_ToNewItemRange(item)]);
        return gNewItemActions[ItemExtension_ToNewItemRange(item)];
    }
}

RECOMP_PATCH PlayerItemAction Player_ItemToItemAction(Player* this, ItemId item) {
    if (item >= ITEM_FD) {
        return PLAYER_IA_NONE;
    } else if (item == ITEM_FC) {
        return PLAYER_IA_LAST_USED;
    } else if (item == ITEM_FISHING_ROD) {
        return PLAYER_IA_FISHING_ROD;
    } else if ((item == ITEM_SWORD_KOKIRI) && (this->transformation == PLAYER_FORM_ZORA)) {
        return PLAYER_IA_ZORA_BOOMERANG;
    } else {
        return getUpdatedItemAction(item);
    }
}

RECOMP_PATCH void Player_InitItemAction(PlayState* play, Player* this, PlayerItemAction itemAction) {
    recomp_printf("getInitialItemAction: %d\n", itemAction);
    this->itemAction = this->heldItemAction = itemAction;
    this->modelGroup = this->nextModelGroup;

    this->stateFlags1 &= ~(PLAYER_STATE1_USING_ZORA_BOOMERANG | PLAYER_STATE1_8);

    this->unk_B08 = 0.0f;
    this->unk_B0C = 0.0f;
    this->unk_B28 = 0;

    if (itemAction < NEW_ACTION_NUMBERS)
        sItemActionInitFuncs[itemAction](play, this);
    else
        gNewItemActionInitFuncs[itemAction-NEW_ACTION_NUMBERS](play, this);
    Player_SetModelGroup(this, this->modelGroup);
}

void newSetUpper(PlayState* play, Player* this) {
    recomp_printf("getHeldItemAction: %d\n", this->heldItemAction);
    Player_SetUpperAction(play, this, (this->heldItemAction < NEW_ACTION_NUMBERS) ?
                    sItemActionUpdateFuncs[this->heldItemAction] :
                    gNewItemActionUpdateFuncs[this->heldItemAction-NEW_ACTION_NUMBERS]);
}

RECOMP_PATCH void func_808309CC(PlayState* play, Player* this) {
    if (Player_UpperAction_ChangeHeldItem == this->upperActionFunc) {
        Player_FinishItemChange(play, this);
    }

    newSetUpper(play,this);
    this->unk_ACC = 0;
    this->idleType = PLAYER_IDLE_DEFAULT;
    Player_DetachHeldActor(play, this);
    this->stateFlags3 &= ~PLAYER_STATE3_START_CHANGING_HELD_ITEM;
}

RECOMP_PATCH s32 Player_UpperAction_ChangeHeldItem(Player* this, PlayState* play) {
    if (PlayerAnimation_Update(play, &this->skelAnimeUpper) ||
        ((Player_ItemToItemAction(this, this->heldItemId) == this->heldItemAction) &&
         (sPlayerUseHeldItem = (sPlayerUseHeldItem || ((this->modelAnimType != PLAYER_ANIMTYPE_3) &&
                                                       (this->heldItemAction != PLAYER_IA_DEKU_STICK) &&
                                                       (play->bButtonAmmoPlusOne == 0)))))) {
        newSetUpper(play,this);
        this->unk_ACC = 0;
        this->idleType = PLAYER_IDLE_DEFAULT;
        sPlayerHeldItemButtonIsHeldDown = sPlayerUseHeldItem;
        return this->upperActionFunc(this, play);
    }

    if (Player_CheckForIdleAnim(this) != IDLE_ANIM_NONE) {
        Player_WaitToFinishItemChange(play, this);
        Player_Anim_PlayOnce(play, this, Player_GetIdleAnim(this));
        this->idleType = PLAYER_IDLE_DEFAULT;
    } else {
        Player_WaitToFinishItemChange(play, this);
    }

    return true;
}

RECOMP_PATCH s32 Player_UpperAction_5(Player* this, PlayState* play) {
    sPlayerUseHeldItem = sPlayerHeldItemButtonIsHeldDown;
    if (sPlayerUseHeldItem || PlayerAnimation_Update(play, &this->skelAnimeUpper)) {
        newSetUpper(play,this);
        PlayerAnimation_PlayLoop(play, &this->skelAnimeUpper, D_8085BE84[PLAYER_ANIMGROUP_wait][this->modelAnimType]);
        this->idleType = PLAYER_IDLE_DEFAULT;
        this->upperActionFunc(this, play);
        return false;
    }
    return true;
}

//z_parameter.c
extern u8 gMagicArrowEquipEffectTex[];

RECOMP_PATCH void Interface_LoadItemIconImpl(PlayState* play, u8 btn) {
    InterfaceContext* interfaceCtx = &play->interfaceCtx;

    ItemId curItem = GET_CUR_FORM_BTN_ITEM(btn);
    if (curItem >= NEW_ACTION_ITEMS) {
        memcpy(&interfaceCtx->iconItemSegment[(u32)btn * ICON_ITEM_TEX_SIZE], gNewItemIcons[ItemExtension_ToNewItemRange(curItem)], ICON_ITEM_TEX_SIZE);
    } else {
        CmpDma_LoadFile(SEGMENT_ROM_START(icon_item_static_yar), curItem,
                        &interfaceCtx->iconItemSegment[(u32)btn * ICON_ITEM_TEX_SIZE], ICON_ITEM_TEX_SIZE);
    }
}

RECOMP_PATCH void Interface_DrawPauseMenuEquippingIcons(PlayState* play) {
    s16 sMagicArrowEffectsR[] = { 255, 100, 255 };
    s16 sMagicArrowEffectsG[] = { 0, 100, 255 };
    s16 sMagicArrowEffectsB[] = { 0, 255, 100 };
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    PauseContext* pauseCtx = &play->pauseCtx;
    s16 temp;

    OPEN_DISPS(play->state.gfxCtx);

    gDPPipeSync(OVERLAY_DISP++);

    // This is needed as `Interface_DrawPauseMenuEquippingIcons` is call immediately
    // after `Interface_DrawAButton`, which sets the view to perspective mode
    Interface_SetOrthoView(interfaceCtx);

    if ((pauseCtx->state == PAUSE_STATE_MAIN) && ((pauseCtx->mainState == PAUSE_MAIN_STATE_EQUIP_ITEM) ||
                                                  (pauseCtx->mainState == PAUSE_MAIN_STATE_EQUIP_MASK))) {
        // Inventory Equip Effects
        gSPSegment(OVERLAY_DISP++, 0x08, pauseCtx->iconItemSegment);
        Gfx_SetupDL42_Overlay(play->state.gfxCtx);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
        gDPSetAlphaCompare(OVERLAY_DISP++, G_AC_THRESHOLD);
        gSPMatrix(OVERLAY_DISP++, &gIdentityMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        pauseCtx->cursorVtx[16].v.ob[0] = pauseCtx->cursorVtx[18].v.ob[0] = pauseCtx->equipAnimX / 10;
        pauseCtx->cursorVtx[17].v.ob[0] = pauseCtx->cursorVtx[19].v.ob[0] =
            pauseCtx->cursorVtx[16].v.ob[0] + (pauseCtx->equipAnimScale / 10);
        pauseCtx->cursorVtx[16].v.ob[1] = pauseCtx->cursorVtx[17].v.ob[1] = pauseCtx->equipAnimY / 10;
        pauseCtx->cursorVtx[18].v.ob[1] = pauseCtx->cursorVtx[19].v.ob[1] =
            pauseCtx->cursorVtx[16].v.ob[1] - (pauseCtx->equipAnimScale / 10);

        if (pauseCtx->equipTargetItem < 0xB5) {
            // Normal Equip (icon goes from the inventory slot to the C button when equipping it)
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, pauseCtx->equipAnimAlpha);
            gSPVertex(OVERLAY_DISP++, &pauseCtx->cursorVtx[16], 4, 0);
            gDPLoadTextureBlock(OVERLAY_DISP++, gItemIcons[pauseCtx->equipTargetItem], G_IM_FMT_RGBA, G_IM_SIZ_32b,
                                ICON_ITEM_TEX_WIDTH, ICON_ITEM_TEX_HEIGHT, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        } else if (pauseCtx->equipTargetItem < NEW_ACTION_ITEMS) {
            // Magic Arrow Equip Effect
            temp = pauseCtx->equipTargetItem - 0xB5;
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, sMagicArrowEffectsR[temp], sMagicArrowEffectsG[temp],
                            sMagicArrowEffectsB[temp], pauseCtx->equipAnimAlpha);

            if ((pauseCtx->equipAnimAlpha > 0) && (pauseCtx->equipAnimAlpha < 255)) {
                temp = (pauseCtx->equipAnimAlpha / 8) / 2;
                pauseCtx->cursorVtx[16].v.ob[0] = pauseCtx->cursorVtx[18].v.ob[0] =
                    pauseCtx->cursorVtx[16].v.ob[0] - temp;
                pauseCtx->cursorVtx[17].v.ob[0] = pauseCtx->cursorVtx[19].v.ob[0] =
                    pauseCtx->cursorVtx[16].v.ob[0] + temp * 2 + 32;
                pauseCtx->cursorVtx[16].v.ob[1] = pauseCtx->cursorVtx[17].v.ob[1] =
                    pauseCtx->cursorVtx[16].v.ob[1] + temp;
                pauseCtx->cursorVtx[18].v.ob[1] = pauseCtx->cursorVtx[19].v.ob[1] =
                    pauseCtx->cursorVtx[16].v.ob[1] - temp * 2 - 32;
            }

            gSPVertex(OVERLAY_DISP++, &pauseCtx->cursorVtx[16], 4, 0);
            gDPLoadTextureBlock(OVERLAY_DISP++, gMagicArrowEquipEffectTex, G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0,
                                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
        } else {
            // Normal Equip (icon goes from the inventory slot to the C button when equipping it)
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, pauseCtx->equipAnimAlpha);
            gSPVertex(OVERLAY_DISP++, &pauseCtx->cursorVtx[16], 4, 0);
            gDPLoadTextureBlock(OVERLAY_DISP++, gNewItemIcons[ItemExtension_ToNewItemRange(pauseCtx->equipTargetItem)], G_IM_FMT_RGBA, G_IM_SIZ_32b,
                                ICON_ITEM_TEX_WIDTH, ICON_ITEM_TEX_HEIGHT, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        }

        gSP1Quadrangle(OVERLAY_DISP++, 0, 2, 3, 1, 0);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

//z_kaleido_item.c

#define EXTRA_ITEM_ROWS 4
#define ITEM_TOTAL_NUM_ROWS (ITEM_GRID_ROWS+EXTRA_ITEM_ROWS)
#define ITEM_TOTAL_NUM_SLOTS (ITEM_NUM_SLOTS+(ITEM_GRID_COLS*EXTRA_ITEM_ROWS))

extern s16 sMagicArrowEffectsR_ovl_kaleido_scope[];
extern s16 sMagicArrowEffectsG_ovl_kaleido_scope[];
extern s16 sMagicArrowEffectsB_ovl_kaleido_scope[];
extern u8 gEquippedItemOutlineTex[];
extern u8 gPlayerFormSlotRestrictions[PLAYER_FORM_MAX][ITEM_NUM_SLOTS];
extern s16 sEquipState;


RECOMP_PATCH void KaleidoScope_DrawItemSelect(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    u16 i;
    u16 j;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL42_Opa(play->state.gfxCtx);

    // Draw a white box around the items that are equipped on the C buttons
    // Loop over c-buttons (i) and vtx offset (j)
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
    for (i = 0, j = ITEM_TOTAL_NUM_SLOTS * 4; i < 3; i++, j += 4) {//The ITEM_TOTAL_NUM_SLOTS causes the appropriate itemVtx index to be used
        if (GET_CUR_FORM_BTN_ITEM(i + 1) != ITEM_NONE) {
            if (GET_CUR_FORM_BTN_SLOT(i + 1) < ITEM_NUM_SLOTS) {
                gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[j], 4, 0);
                POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
            } else if (GET_CUR_FORM_BTN_SLOT(i + 1) >= MAX_REGULAR_SLOTS && GET_CUR_FORM_BTN_SLOT(i + 1) < MAX_REGULAR_SLOTS+(EXTRA_ITEM_ROWS*ITEM_GRID_COLS)) {
                gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[j], 4, 0);
                POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
            }
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);

    // Draw the item icons
    // Loop over slots (i) and vtx offset (j)
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    for (j = 0, i = 0; i < ITEM_NUM_SLOTS; i++, j += 4) {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);

        if (((void)0, gSaveContext.save.saveInfo.inventory.items[i]) != ITEM_NONE) {
            if ((pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) && (pauseCtx->pageIndex == PAUSE_ITEM) &&
                (pauseCtx->cursorSpecialPos == 0) && gPlayerFormSlotRestrictions[GET_PLAYER_FORM][i]) {
                if ((sEquipState == EQUIP_STATE_MAGIC_ARROW_HOVER_OVER_BOW_SLOT) && (i == SLOT_ARROW_ICE)) {
                    // Possible bug:
                    // Supposed to be `SLOT_BOW`, unchanged from OoT, instead increase size of ice arrow icon
                    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, sMagicArrowEffectsR_ovl_kaleido_scope[pauseCtx->equipTargetItem - 0xB5],
                                    sMagicArrowEffectsG_ovl_kaleido_scope[pauseCtx->equipTargetItem - 0xB5],
                                    sMagicArrowEffectsB_ovl_kaleido_scope[pauseCtx->equipTargetItem - 0xB5], pauseCtx->alpha);

                    pauseCtx->itemVtx[j + 0].v.ob[0] = pauseCtx->itemVtx[j + 2].v.ob[0] =
                        pauseCtx->itemVtx[j + 0].v.ob[0] - 2;
                    pauseCtx->itemVtx[j + 1].v.ob[0] = pauseCtx->itemVtx[j + 3].v.ob[0] =
                        pauseCtx->itemVtx[j + 0].v.ob[0] + 32;
                    pauseCtx->itemVtx[j + 0].v.ob[1] = pauseCtx->itemVtx[j + 1].v.ob[1] =
                        pauseCtx->itemVtx[j + 0].v.ob[1] + 2;
                    pauseCtx->itemVtx[j + 2].v.ob[1] = pauseCtx->itemVtx[j + 3].v.ob[1] =
                        pauseCtx->itemVtx[j + 0].v.ob[1] - 32;

                } else if (i == pauseCtx->cursorSlot[PAUSE_ITEM]) {
                    // Increase the size of the selected item
                    pauseCtx->itemVtx[j + 0].v.ob[0] = pauseCtx->itemVtx[j + 2].v.ob[0] =
                        pauseCtx->itemVtx[j + 0].v.ob[0] - 2;
                    pauseCtx->itemVtx[j + 1].v.ob[0] = pauseCtx->itemVtx[j + 3].v.ob[0] =
                        pauseCtx->itemVtx[j + 0].v.ob[0] + 32;
                    pauseCtx->itemVtx[j + 0].v.ob[1] = pauseCtx->itemVtx[j + 1].v.ob[1] =
                        pauseCtx->itemVtx[j + 0].v.ob[1] + 2;
                    pauseCtx->itemVtx[j + 2].v.ob[1] = pauseCtx->itemVtx[j + 3].v.ob[1] =
                        pauseCtx->itemVtx[j + 0].v.ob[1] - 32;
                }
            }

            gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[j + 0], 4, 0);
            if (gSaveContext.save.saveInfo.inventory.items[i] < NEW_ACTION_ITEMS) {
                KaleidoScope_DrawTexQuadRGBA32(
                    play->state.gfxCtx, gItemIcons[((void)0, gSaveContext.save.saveInfo.inventory.items[i])], 32, 32, 0);
            } else {
                KaleidoScope_DrawTexQuadRGBA32(
                    play->state.gfxCtx, gNewItemIcons[ItemExtension_ToNewItemRange(gSaveContext.save.saveInfo.inventory.items[i])], 32, 32, 0);
            }
        }
    }

    for (; i < ITEM_TOTAL_NUM_SLOTS; i++, j += 4) {//Index starts from previous i,j values
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
        s16 newItem = gNewInventoryItemSlots[i-ITEM_NUM_SLOTS];
        if (newItem != ITEM_NONE) {
            if ((pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) && (pauseCtx->pageIndex == PAUSE_ITEM) &&
                (pauseCtx->cursorSpecialPos == 0) /*&& gPlayerFormSlotRestrictions[GET_PLAYER_FORM][i]*/) {
                if (i == pauseCtx->cursorSlot[PAUSE_ITEM]) {
                    // Increase the size of the selected item
                    pauseCtx->itemVtx[j + 0].v.ob[0] = pauseCtx->itemVtx[j + 2].v.ob[0] =
                        pauseCtx->itemVtx[j + 0].v.ob[0] - 2;
                    pauseCtx->itemVtx[j + 1].v.ob[0] = pauseCtx->itemVtx[j + 3].v.ob[0] =
                        pauseCtx->itemVtx[j + 0].v.ob[0] + 32;
                    pauseCtx->itemVtx[j + 0].v.ob[1] = pauseCtx->itemVtx[j + 1].v.ob[1] =
                        pauseCtx->itemVtx[j + 0].v.ob[1] + 2;
                    pauseCtx->itemVtx[j + 2].v.ob[1] = pauseCtx->itemVtx[j + 3].v.ob[1] =
                        pauseCtx->itemVtx[j + 0].v.ob[1] - 32;
                }
            }

            gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[j + 0], 4, 0);
            if (newItem < NEW_ACTION_ITEMS) {
                KaleidoScope_DrawTexQuadRGBA32(
                    play->state.gfxCtx, gItemIcons[newItem], 32, 32, 0);
            } else {
                KaleidoScope_DrawTexQuadRGBA32(
                    play->state.gfxCtx, gNewItemIcons[ItemExtension_ToNewItemRange(newItem)], 32, 32, 0);
            }
        }
    }

    // Draw the ammo digits
    if (pauseCtx->pageIndex == PAUSE_ITEM) {
        if ((pauseCtx->state == PAUSE_STATE_MAIN) &&
            ((pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) || (pauseCtx->mainState == PAUSE_MAIN_STATE_EQUIP_ITEM)) &&
            (pauseCtx->state != PAUSE_STATE_SAVEPROMPT) && !IS_PAUSE_STATE_GAMEOVER(pauseCtx)) {
            Gfx_SetupDL39_Opa(play->state.gfxCtx);
            gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

            // Loop over slots (i) and ammoIndex (j)
            for (j = 0, i = 0; i < ITEM_NUM_SLOTS; i++) {
                if (gAmmoItems[i] != ITEM_NONE) {
                    if (((void)0, gSaveContext.save.saveInfo.inventory.items[i]) != ITEM_NONE) {
                        KaleidoScope_DrawAmmoCount(pauseCtx, play->state.gfxCtx,
                                                   ((void)0, gSaveContext.save.saveInfo.inventory.items[i]), j);
                    }
                    j++;
                }
            }
            Gfx_SetupDL42_Opa(play->state.gfxCtx);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

RECOMP_PATCH void Kaleido_LoadItemNameStatic(void* segment, u32 texIndex) {
    if (texIndex < NEW_ACTION_ITEMS)
        CmpDma_LoadFile(SEGMENT_ROM_START(item_name_static), texIndex, segment, 0x400);
    else
        memcpy(segment, gNewItemNames[ItemExtension_ToNewItemRange(texIndex)], 0x400);
}

static PlayState* MyTempPlay;
PauseContext* MyTempPauseCtx;
static GraphicsContext* MyTempGfxCtx;
#define ItemPageOffsetY (ITEM_GRID_CELL_HEIGHT*(pauseCtx->cursorYIndex[PAUSE_ITEM]<4?0:pauseCtx->cursorYIndex[PAUSE_ITEM]-3))

RECOMP_HOOK("KaleidoScope_SetVertices")
void SetupKaleidoScope_SetVertices(PlayState* play, GraphicsContext* gfxCtx) {
     MyTempPlay = play;
     MyTempGfxCtx = gfxCtx;
}

RECOMP_HOOK_RETURN("KaleidoScope_SetVertices")
void FinalizeKaleidoScope_SetVertices() {
    PauseContext* pauseCtx = &MyTempPlay->pauseCtx;
    s16 i;
    s16 j;
    s16 k;
    s16 vtx_x;
    s16 vtx_y;

    if (pauseCtx->pageIndex != PAUSE_QUEST) {
        //pauseCtx->itemPageVtx = GRAPH_ALLOC(MyTempGfxCtx, ((PAGE_BG_QUADS + VTX_PAGE_ITEM_QUADS) * 4) * sizeof(Vtx));
        //KaleidoScope_SetPageVertices(MyTempPlay, pauseCtx->itemPageVtx, VTX_PAGE_ITEM, VTX_PAGE_ITEM_QUADS);

        pauseCtx->itemVtx = GRAPH_ALLOC(MyTempGfxCtx, ((QUAD_ITEM_MAX + (ITEM_GRID_COLS*EXTRA_ITEM_ROWS)) * 4) * sizeof(Vtx));

        // QUAD_ITEM_GRID_FIRST..QUAD_ITEM_GRID_LAST

        // Loop over grid rows
        for (k = 0, i = 0, vtx_y = (ITEM_GRID_ROWS * ITEM_GRID_CELL_HEIGHT) / 2 - 6; k < ITEM_GRID_ROWS+EXTRA_ITEM_ROWS;
             k++, vtx_y -= ITEM_GRID_CELL_HEIGHT) {
            // Loop over grid columns
            for (vtx_x = 0 - (ITEM_GRID_COLS * ITEM_GRID_CELL_WIDTH) / 2, j = 0; j < ITEM_GRID_COLS;
                 j++, i += 4, vtx_x += ITEM_GRID_CELL_WIDTH) {
                pauseCtx->itemVtx[i + 0].v.ob[0] = pauseCtx->itemVtx[i + 2].v.ob[0] = vtx_x + ITEM_GRID_QUAD_MARGIN;
                pauseCtx->itemVtx[i + 1].v.ob[0] = pauseCtx->itemVtx[i + 3].v.ob[0] =
                    pauseCtx->itemVtx[i + 0].v.ob[0] + ITEM_GRID_QUAD_WIDTH;

                pauseCtx->itemVtx[i + 0].v.ob[1] = pauseCtx->itemVtx[i + 1].v.ob[1] =
                    vtx_y + pauseCtx->offsetY + ItemPageOffsetY - ITEM_GRID_QUAD_MARGIN;
                pauseCtx->itemVtx[i + 2].v.ob[1] = pauseCtx->itemVtx[i + 3].v.ob[1] =
                    pauseCtx->itemVtx[i + 0].v.ob[1] - ITEM_GRID_QUAD_WIDTH;

                pauseCtx->itemVtx[i + 0].v.ob[2] = pauseCtx->itemVtx[i + 1].v.ob[2] = pauseCtx->itemVtx[i + 2].v.ob[2] =
                    pauseCtx->itemVtx[i + 3].v.ob[2] = 0;

                pauseCtx->itemVtx[i + 0].v.flag = pauseCtx->itemVtx[i + 1].v.flag = pauseCtx->itemVtx[i + 2].v.flag =
                    pauseCtx->itemVtx[i + 3].v.flag = 0;

                pauseCtx->itemVtx[i + 0].v.tc[0] = pauseCtx->itemVtx[i + 0].v.tc[1] = pauseCtx->itemVtx[i + 1].v.tc[1] =
                    pauseCtx->itemVtx[i + 2].v.tc[0] = 0;

                pauseCtx->itemVtx[i + 1].v.tc[0] = pauseCtx->itemVtx[i + 2].v.tc[1] = pauseCtx->itemVtx[i + 3].v.tc[0] =
                    pauseCtx->itemVtx[i + 3].v.tc[1] = ITEM_GRID_QUAD_TEX_SIZE * (1 << 5);

                pauseCtx->itemVtx[i + 0].v.cn[0] = pauseCtx->itemVtx[i + 1].v.cn[0] = pauseCtx->itemVtx[i + 2].v.cn[0] =
                    pauseCtx->itemVtx[i + 3].v.cn[0] = pauseCtx->itemVtx[i + 0].v.cn[1] =
                        pauseCtx->itemVtx[i + 1].v.cn[1] = pauseCtx->itemVtx[i + 2].v.cn[1] =
                            pauseCtx->itemVtx[i + 3].v.cn[1] = pauseCtx->itemVtx[i + 0].v.cn[2] =
                                pauseCtx->itemVtx[i + 1].v.cn[2] = pauseCtx->itemVtx[i + 2].v.cn[2] =
                                    pauseCtx->itemVtx[i + 3].v.cn[2] = 255;

                pauseCtx->itemVtx[i + 0].v.cn[3] = pauseCtx->itemVtx[i + 1].v.cn[3] = pauseCtx->itemVtx[i + 2].v.cn[3] =
                    pauseCtx->itemVtx[i + 3].v.cn[3] = 255;
            }
        }

        for (j = EQUIP_SLOT_C_LEFT; j <= EQUIP_SLOT_C_RIGHT; j++, i += 4) {
            s16 modSlot = GET_CUR_FORM_BTN_SLOT(j);

            if (modSlot != ITEM_NONE && (modSlot < ITEM_NUM_SLOTS || modSlot >= MAX_REGULAR_SLOTS)) {
                if (modSlot >= MAX_REGULAR_SLOTS)
                    modSlot -= MASK_NUM_SLOTS;
                k = modSlot * 4;

                pauseCtx->itemVtx[i + 0].v.ob[0] = pauseCtx->itemVtx[i + 2].v.ob[0] =
                    pauseCtx->itemVtx[k].v.ob[0] + ITEM_GRID_SELECTED_QUAD_MARGIN;

                pauseCtx->itemVtx[i + 1].v.ob[0] = pauseCtx->itemVtx[i + 3].v.ob[0] =
                    pauseCtx->itemVtx[i + 0].v.ob[0] + ITEM_GRID_SELECTED_QUAD_WIDTH;

                pauseCtx->itemVtx[i + 0].v.ob[1] = pauseCtx->itemVtx[i + 1].v.ob[1] =
                    pauseCtx->itemVtx[k].v.ob[1] - ITEM_GRID_SELECTED_QUAD_MARGIN;

                pauseCtx->itemVtx[i + 2].v.ob[1] = pauseCtx->itemVtx[i + 3].v.ob[1] =
                    pauseCtx->itemVtx[i + 0].v.ob[1] - ITEM_GRID_SELECTED_QUAD_WIDTH;

                pauseCtx->itemVtx[i + 0].v.ob[2] = pauseCtx->itemVtx[i + 1].v.ob[2] = pauseCtx->itemVtx[i + 2].v.ob[2] =
                    pauseCtx->itemVtx[i + 3].v.ob[2] = 0;

                pauseCtx->itemVtx[i + 0].v.flag = pauseCtx->itemVtx[i + 1].v.flag = pauseCtx->itemVtx[i + 2].v.flag =
                    pauseCtx->itemVtx[i + 3].v.flag = 0;

                pauseCtx->itemVtx[i + 0].v.tc[0] = pauseCtx->itemVtx[i + 0].v.tc[1] = pauseCtx->itemVtx[i + 1].v.tc[1] =
                    pauseCtx->itemVtx[i + 2].v.tc[0] = 0;

                pauseCtx->itemVtx[i + 1].v.tc[0] = pauseCtx->itemVtx[i + 2].v.tc[1] = pauseCtx->itemVtx[i + 3].v.tc[0] =
                    pauseCtx->itemVtx[i + 3].v.tc[1] = ITEM_GRID_SELECTED_QUAD_TEX_SIZE * (1 << 5);

                pauseCtx->itemVtx[i + 0].v.cn[0] = pauseCtx->itemVtx[i + 1].v.cn[0] = pauseCtx->itemVtx[i + 2].v.cn[0] =
                    pauseCtx->itemVtx[i + 3].v.cn[0] = pauseCtx->itemVtx[i + 0].v.cn[1] =
                        pauseCtx->itemVtx[i + 1].v.cn[1] = pauseCtx->itemVtx[i + 2].v.cn[1] =
                            pauseCtx->itemVtx[i + 3].v.cn[1] = pauseCtx->itemVtx[i + 0].v.cn[2] =
                                pauseCtx->itemVtx[i + 1].v.cn[2] = pauseCtx->itemVtx[i + 2].v.cn[2] =
                                    pauseCtx->itemVtx[i + 3].v.cn[2] = 255;

                pauseCtx->itemVtx[i + 0].v.cn[3] = pauseCtx->itemVtx[i + 1].v.cn[3] = pauseCtx->itemVtx[i + 2].v.cn[3] =
                    pauseCtx->itemVtx[i + 3].v.cn[3] = pauseCtx->alpha;
            } else {
                // No item equipped on the C button, put the quad out of view

                pauseCtx->itemVtx[i + 2].v.ob[0] = -300;
                pauseCtx->itemVtx[i + 0].v.ob[0] = pauseCtx->itemVtx[i + 2].v.ob[0];

                pauseCtx->itemVtx[i + 1].v.ob[0] = pauseCtx->itemVtx[i + 3].v.ob[0] =
                    pauseCtx->itemVtx[i + 0].v.ob[0] + ITEM_GRID_SELECTED_QUAD_WIDTH;

                pauseCtx->itemVtx[i + 0].v.ob[1] = pauseCtx->itemVtx[i + 1].v.ob[1] = 300;
                pauseCtx->itemVtx[i + 2].v.ob[1] = pauseCtx->itemVtx[i + 3].v.ob[1] =
                    pauseCtx->itemVtx[i + 0].v.ob[1] - ITEM_GRID_SELECTED_QUAD_HEIGHT;
            }
        }
    }

    //Stops extra items from repositioning the mask grid selection markers to undesirable positions
    if (pauseCtx->pageIndex != PAUSE_MAP) {
        i = MASK_NUM_SLOTS*4;
        for (j = EQUIP_SLOT_C_LEFT; j <= EQUIP_SLOT_C_RIGHT; j++, i += 4) {
            s16 modSlot = GET_CUR_FORM_BTN_SLOT(j);
            if (modSlot != ITEM_NONE && modSlot >= MAX_REGULAR_SLOTS) {
                pauseCtx->maskVtx[i + 2].v.ob[0] = -300;
                pauseCtx->maskVtx[i + 0].v.ob[0] = pauseCtx->maskVtx[i + 2].v.ob[0];

                pauseCtx->maskVtx[i + 1].v.ob[0] = pauseCtx->maskVtx[i + 3].v.ob[0] =
                    pauseCtx->maskVtx[i + 0].v.ob[0] + MASK_GRID_SELECTED_QUAD_WIDTH;

                pauseCtx->maskVtx[i + 0].v.ob[1] = pauseCtx->maskVtx[i + 1].v.ob[1] = 300;
                pauseCtx->maskVtx[i + 2].v.ob[1] = pauseCtx->maskVtx[i + 3].v.ob[1] =
                    pauseCtx->maskVtx[i + 0].v.ob[1] - MASK_GRID_SELECTED_QUAD_HEIGHT;
            }
        }
    }
}

extern s16 sAmmoRectHeight[];
static s16 MyTempOffsetY;
static u16 MyTempAmmoIndex;

RECOMP_HOOK("KaleidoScope_SetPageVertices")
void SetupKaleidoScope_SetPageVertices(PlayState* play, Vtx* vtx, s16 vtxPage, s16 numQuads) {
    PauseContext* pauseCtx = &play->pauseCtx;
    MyTempPlay = play;
    MyTempOffsetY = pauseCtx->offsetY;
    if (vtxPage == VTX_PAGE_ITEM)
        pauseCtx->offsetY += ItemPageOffsetY;
}

RECOMP_HOOK_RETURN("KaleidoScope_SetPageVertices")
void FinalizeKaleidoScope_SetPageVertices() {
    PauseContext* pauseCtx = &MyTempPlay->pauseCtx;
    pauseCtx->offsetY = MyTempOffsetY;
}

RECOMP_HOOK("KaleidoScope_DrawAmmoCount")
void SetupKaleidoScope_DrawAmmoCount(PauseContext* pauseCtx, GraphicsContext* gfxCtx, s16 item, u16 ammoIndex) {
    MyTempPauseCtx = pauseCtx;
    MyTempAmmoIndex = ammoIndex;
    sAmmoRectHeight[ammoIndex] -= ItemPageOffsetY;
}

RECOMP_HOOK_RETURN("KaleidoScope_DrawAmmoCount")
void FinalizeKaleidoScope_DrawAmmoCount() {
    PauseContext* pauseCtx = MyTempPauseCtx;
    sAmmoRectHeight[MyTempAmmoIndex] += ItemPageOffsetY;
}

#define STICK_MOVEMENT_THRESHOLD 30

extern u8 sPlayerFormItems[];
extern s16 sEquipMagicArrowSlotHoldTimer;
extern s16 sEquipAnimTimer;

s16 getItemFromFullInventoryPage(s16 inventoryPos) {
    if (inventoryPos < ITEM_NUM_SLOTS)
        return gSaveContext.save.saveInfo.inventory.items[inventoryPos];
    else
        return gNewInventoryItemSlots[inventoryPos-ITEM_NUM_SLOTS];
}


RECOMP_PATCH void KaleidoScope_UpdateItemCursor(PlayState* play) {
    s32 pad1;
    PauseContext* pauseCtx = &play->pauseCtx;
    MessageContext* msgCtx = &play->msgCtx;
    u16 vtxIndex;
    u16 cursorItem;
    u16 cursorSlot;
    u8 magicArrowIndex;
    s16 cursorPoint;
    s16 cursorXIndex;
    s16 cursorYIndex;
    s16 oldCursorPoint;
    s16 moveCursorResult;
    s16 pad2;

    pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_WHITE;
    pauseCtx->nameColorSet = PAUSE_NAME_COLOR_SET_WHITE;

    if ((pauseCtx->state == PAUSE_STATE_MAIN) && (pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) &&
        (pauseCtx->pageIndex == PAUSE_ITEM) && !pauseCtx->itemDescriptionOn) {
        moveCursorResult = PAUSE_CURSOR_RESULT_NONE;
        oldCursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];

        cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];

        // Move cursor left/right
        if (pauseCtx->cursorSpecialPos == 0) {
            // cursor is currently on a slot
            pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;

            if (ABS_ALT(pauseCtx->stickAdjX) > STICK_MOVEMENT_THRESHOLD) {
                cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
                cursorXIndex = pauseCtx->cursorXIndex[PAUSE_ITEM];
                cursorYIndex = pauseCtx->cursorYIndex[PAUSE_ITEM];

                // Search for slot to move to
                while (moveCursorResult == PAUSE_CURSOR_RESULT_NONE) {
                    if (pauseCtx->stickAdjX < -STICK_MOVEMENT_THRESHOLD) {
                        // move cursor left
                        pauseCtx->cursorShrinkRate = 4.0f;
                        if (pauseCtx->cursorXIndex[PAUSE_ITEM] != 0) {
                            pauseCtx->cursorXIndex[PAUSE_ITEM]--;
                            pauseCtx->cursorPoint[PAUSE_ITEM]--;
                            moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                        } else {
                            pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorXIndex;
                            pauseCtx->cursorYIndex[PAUSE_ITEM]++;

                            if (pauseCtx->cursorYIndex[PAUSE_ITEM] >= ITEM_TOTAL_NUM_ROWS) {
                                pauseCtx->cursorYIndex[PAUSE_ITEM] = 0;
                            }

                            pauseCtx->cursorPoint[PAUSE_ITEM] =
                                pauseCtx->cursorXIndex[PAUSE_ITEM] + (pauseCtx->cursorYIndex[PAUSE_ITEM] * ITEM_GRID_COLS);

                            if (pauseCtx->cursorPoint[PAUSE_ITEM] >= ITEM_TOTAL_NUM_SLOTS) {
                                pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorXIndex[PAUSE_ITEM];
                            }

                            if (cursorYIndex == pauseCtx->cursorYIndex[PAUSE_ITEM]) {
                                pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorXIndex;
                                pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;

                                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);

                                moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                            }
                        }
                    } else if (pauseCtx->stickAdjX > STICK_MOVEMENT_THRESHOLD) {
                        // move cursor right
                        pauseCtx->cursorShrinkRate = 4.0f;
                        if (pauseCtx->cursorXIndex[PAUSE_ITEM] <= ITEM_GRID_COLS-2) {
                            pauseCtx->cursorXIndex[PAUSE_ITEM]++;
                            pauseCtx->cursorPoint[PAUSE_ITEM]++;
                            moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                        } else {
                            pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorXIndex;
                            pauseCtx->cursorYIndex[PAUSE_ITEM]++;

                            if (pauseCtx->cursorYIndex[PAUSE_ITEM] >= ITEM_TOTAL_NUM_ROWS) {
                                pauseCtx->cursorYIndex[PAUSE_ITEM] = 0;
                            }

                            pauseCtx->cursorPoint[PAUSE_ITEM] =
                                pauseCtx->cursorXIndex[PAUSE_ITEM] + (pauseCtx->cursorYIndex[PAUSE_ITEM] * ITEM_GRID_COLS);

                            if (pauseCtx->cursorPoint[PAUSE_ITEM] >= ITEM_TOTAL_NUM_SLOTS) {
                                pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorXIndex[PAUSE_ITEM];
                            }

                            if (cursorYIndex == pauseCtx->cursorYIndex[PAUSE_ITEM]) {
                                pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorXIndex;
                                pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;

                                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);

                                moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                            }
                        }
                    }
                }

                if (moveCursorResult == PAUSE_CURSOR_RESULT_SLOT) {
                    cursorItem = getItemFromFullInventoryPage(pauseCtx->cursorPoint[PAUSE_ITEM]);
                }
            }
        } else if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
            if (pauseCtx->stickAdjX > STICK_MOVEMENT_THRESHOLD) {
                KaleidoScope_MoveCursorFromSpecialPos(play);
                cursorYIndex = 0;
                cursorXIndex = 0;
                cursorPoint = 0; // top row, left column (SLOT_OCARINA)

                // Search for slot to move to
                while (true) {
                    // Check if current cursor has an item in its slot
                    if (getItemFromFullInventoryPage(cursorPoint) != ITEM_NONE) {
                        pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                        pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorXIndex;
                        pauseCtx->cursorYIndex[PAUSE_ITEM] = cursorYIndex;
                        moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                        break;
                    }

                    // move 1 row down and retry
                    cursorYIndex++;
                    cursorPoint += ITEM_GRID_COLS;
                    if (cursorYIndex < ITEM_TOTAL_NUM_ROWS) {
                        continue;
                    }

                    // move 1 column right and retry
                    cursorYIndex = 0;
                    cursorPoint = cursorXIndex + 1;
                    cursorXIndex = cursorPoint;
                    if (cursorXIndex < ITEM_GRID_COLS) {
                        continue;
                    }

                    // No item available
                    KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);
                    break;
                }
            }
        } else { // PAUSE_CURSOR_PAGE_RIGHT
            if (pauseCtx->stickAdjX < -STICK_MOVEMENT_THRESHOLD) {
                KaleidoScope_MoveCursorFromSpecialPos(play);
                cursorXIndex = ITEM_GRID_COLS-1;
                cursorPoint = ITEM_GRID_COLS-1; // top row, right columne (SLOT_TRADE_DEED)
                cursorYIndex = 0;

                // Search for slot to move to
                while (true) {
                    // Check if current cursor has an item in its slot
                    if (getItemFromFullInventoryPage(cursorPoint) != ITEM_NONE) {
                        pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                        pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorXIndex;
                        pauseCtx->cursorYIndex[PAUSE_ITEM] = cursorYIndex;
                        moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                        break;
                    }

                    // move 1 row down and retry
                    cursorYIndex++;
                    cursorPoint += ITEM_GRID_COLS;
                    if (cursorYIndex < ITEM_TOTAL_NUM_ROWS) {
                        continue;
                    }

                    // move 1 column left and retry
                    cursorYIndex = 0;
                    cursorPoint = cursorXIndex - 1;
                    cursorXIndex = cursorPoint;
                    if (cursorXIndex >= 0) {
                        continue;
                    }

                    // No item available
                    KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
                    break;
                }
            }
        }

        if (pauseCtx->cursorSpecialPos == 0) {
            // move cursor up/down
            if (ABS_ALT(pauseCtx->stickAdjY) > STICK_MOVEMENT_THRESHOLD) {
                moveCursorResult = PAUSE_CURSOR_RESULT_NONE;

                cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
                cursorYIndex = pauseCtx->cursorYIndex[PAUSE_ITEM];

                while (moveCursorResult == PAUSE_CURSOR_RESULT_NONE) {
                    if (pauseCtx->stickAdjY > STICK_MOVEMENT_THRESHOLD) {
                        // move cursor up
                        moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                        if (pauseCtx->cursorYIndex[PAUSE_ITEM] != 0) {
                            pauseCtx->cursorYIndex[PAUSE_ITEM]--;
                            pauseCtx->cursorShrinkRate = 4.0f;
                            pauseCtx->cursorPoint[PAUSE_ITEM] -= ITEM_GRID_COLS;
                            moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                        } else {
                            pauseCtx->cursorYIndex[PAUSE_ITEM] = cursorYIndex;
                            pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                        }
                    } else if (pauseCtx->stickAdjY < -STICK_MOVEMENT_THRESHOLD) {
                        // move cursor down
                        moveCursorResult = PAUSE_CURSOR_RESULT_SPECIAL_POS;
                        if (pauseCtx->cursorYIndex[PAUSE_ITEM] < ITEM_TOTAL_NUM_ROWS-1) {
                            pauseCtx->cursorYIndex[PAUSE_ITEM]++;
                            pauseCtx->cursorShrinkRate = 4.0f;
                            pauseCtx->cursorPoint[PAUSE_ITEM] += ITEM_GRID_COLS;
                            moveCursorResult = PAUSE_CURSOR_RESULT_SLOT;
                        } else {
                            pauseCtx->cursorYIndex[PAUSE_ITEM] = cursorYIndex;
                            pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                        }
                    }
                }
            }

            cursorSlot = pauseCtx->cursorPoint[PAUSE_ITEM];
            pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;

            if (moveCursorResult == PAUSE_CURSOR_RESULT_SLOT) {
                cursorItem = getItemFromFullInventoryPage(pauseCtx->cursorPoint[PAUSE_ITEM]);
            } else if (moveCursorResult != PAUSE_CURSOR_RESULT_SPECIAL_POS) {
                cursorItem = getItemFromFullInventoryPage(pauseCtx->cursorPoint[PAUSE_ITEM]);
            }

            if (cursorItem == ITEM_NONE) {
                cursorItem = PAUSE_ITEM_NONE;
                pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_WHITE;
            }

            if ((cursorItem != (u32)PAUSE_ITEM_NONE) && (msgCtx->msgLength == 0)) {
                if (gSaveContext.buttonStatus[EQUIP_SLOT_A] == BTN_DISABLED) {
                    gSaveContext.buttonStatus[EQUIP_SLOT_A] = BTN_ENABLED;
                    gSaveContext.hudVisibility = HUD_VISIBILITY_IDLE;
                    Interface_SetHudVisibility(HUD_VISIBILITY_ALL);
                }
            } else if (gSaveContext.buttonStatus[EQUIP_SLOT_A] != BTN_DISABLED) {
                gSaveContext.buttonStatus[EQUIP_SLOT_A] = BTN_DISABLED;
                gSaveContext.hudVisibility = HUD_VISIBILITY_IDLE;
                Interface_SetHudVisibility(HUD_VISIBILITY_ALL);
            }

            pauseCtx->cursorItem[PAUSE_ITEM] = cursorItem;
            pauseCtx->cursorSlot[PAUSE_ITEM] = cursorSlot;
            if (cursorItem != PAUSE_ITEM_NONE) {
                // Equip item to the C buttons
                if ((pauseCtx->debugEditor == DEBUG_EDITOR_NONE) && !pauseCtx->itemDescriptionOn &&
                    (pauseCtx->state == PAUSE_STATE_MAIN) && (pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) &&
                    CHECK_BTN_ANY(CONTROLLER1(&play->state)->press.button, BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT)) {

                    // Ensure that a transformation mask can not be unequipped while being used
                    if (GET_PLAYER_FORM != PLAYER_FORM_HUMAN) {
                        if (1) {}
                        if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_CLEFT)) {
                            if (sPlayerFormItems[GET_PLAYER_FORM] == BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT)) {
                                Audio_PlaySfx(NA_SE_SY_ERROR);
                                return;
                            }
                        } else if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_CDOWN)) {
                            if (sPlayerFormItems[GET_PLAYER_FORM] == BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN)) {
                                Audio_PlaySfx(NA_SE_SY_ERROR);
                                return;
                            }
                        } else if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_CRIGHT)) {
                            if (sPlayerFormItems[GET_PLAYER_FORM] == BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT)) {
                                Audio_PlaySfx(NA_SE_SY_ERROR);
                                return;
                            }
                        }
                    }

                    // Ensure that a non-transformation mask can not be unequipped while being used
                    if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_CLEFT)) {
                        if ((Player_GetCurMaskItemId(play) != ITEM_NONE) &&
                            (Player_GetCurMaskItemId(play) == BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT))) {
                            Audio_PlaySfx(NA_SE_SY_ERROR);
                            return;
                        }
                        pauseCtx->equipTargetCBtn = PAUSE_EQUIP_C_LEFT;
                    } else if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_CDOWN)) {
                        if ((Player_GetCurMaskItemId(play) != ITEM_NONE) &&
                            (Player_GetCurMaskItemId(play) == BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN))) {
                            Audio_PlaySfx(NA_SE_SY_ERROR);
                            return;
                        }
                        pauseCtx->equipTargetCBtn = PAUSE_EQUIP_C_DOWN;
                    } else if (CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_CRIGHT)) {
                        if ((Player_GetCurMaskItemId(play) != ITEM_NONE) &&
                            (Player_GetCurMaskItemId(play) == BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT))) {
                            Audio_PlaySfx(NA_SE_SY_ERROR);
                            return;
                        }
                        pauseCtx->equipTargetCBtn = PAUSE_EQUIP_C_RIGHT;
                    }

                    // Equip item to the C buttons
                    pauseCtx->equipTargetItem = cursorItem;
                    pauseCtx->equipTargetSlot = cursorSlot;
                    if (pauseCtx->equipTargetSlot >= ITEM_NUM_SLOTS)
                        pauseCtx->equipTargetSlot += MASK_NUM_SLOTS;
                    pauseCtx->mainState = PAUSE_MAIN_STATE_EQUIP_ITEM;
                    vtxIndex = cursorSlot * 4;
                    pauseCtx->equipAnimX = pauseCtx->itemVtx[vtxIndex].v.ob[0] * 10;
                    pauseCtx->equipAnimY = pauseCtx->itemVtx[vtxIndex].v.ob[1] * 10;
                    pauseCtx->equipAnimAlpha = 255;
                    sEquipMagicArrowSlotHoldTimer = 0;
                    sEquipState = EQUIP_STATE_MOVE_TO_C_BTN;
                    sEquipAnimTimer = 10;

                    if ((pauseCtx->equipTargetItem == ITEM_ARROW_FIRE) ||
                        (pauseCtx->equipTargetItem == ITEM_ARROW_ICE) ||
                        (pauseCtx->equipTargetItem == ITEM_ARROW_LIGHT)) {
                        magicArrowIndex = 0;
                        if (pauseCtx->equipTargetItem == ITEM_ARROW_ICE) {
                            magicArrowIndex = 1;
                        }
                        if (pauseCtx->equipTargetItem == ITEM_ARROW_LIGHT) {
                            magicArrowIndex = 2;
                        }
                        Audio_PlaySfx(NA_SE_SY_SET_FIRE_ARROW + magicArrowIndex);
                        pauseCtx->equipTargetItem = 0xB5 + magicArrowIndex;
                        pauseCtx->equipAnimAlpha = sEquipState = 0; // EQUIP_STATE_MAGIC_ARROW_GROW_ORB
                        sEquipAnimTimer = 6;
                    } else {
                        Audio_PlaySfx(NA_SE_SY_DECIDE);
                    }
                } else if ((pauseCtx->debugEditor == DEBUG_EDITOR_NONE) && (pauseCtx->state == PAUSE_STATE_MAIN) &&
                           (pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) &&
                           CHECK_BTN_ALL(CONTROLLER1(&play->state)->press.button, BTN_A) && (msgCtx->msgLength == 0)) {
                    // Give description on item through a message box
                    pauseCtx->itemDescriptionOn = true;
                    if (pauseCtx->cursorYIndex[PAUSE_ITEM] < 2) {
                        func_801514B0(play, 0x1700 + pauseCtx->cursorItem[PAUSE_ITEM], 3);
                    } else {
                        func_801514B0(play, 0x1700 + pauseCtx->cursorItem[PAUSE_ITEM], 1);
                    }
                }
            }
        } else {
            pauseCtx->cursorItem[PAUSE_ITEM] = PAUSE_ITEM_NONE;
        }

        if (oldCursorPoint != pauseCtx->cursorPoint[PAUSE_ITEM]) {
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        }
    } else if ((pauseCtx->mainState == PAUSE_MAIN_STATE_EQUIP_ITEM) && (pauseCtx->pageIndex == PAUSE_ITEM)) {
        pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;
    }
}

extern f32 sItemMaskCursorsY[];
static s16 MyTempCursorYIndex;

RECOMP_HOOK("KaleidoScope_UpdateCursorSize")
void Enter_KaleidoScope_UpdateCursorSize(PlayState* play) {
    MyTempPlay = play;
    PauseContext* pauseCtx = &play->pauseCtx;
    if (pauseCtx->cursorSpecialPos == 0 && pauseCtx->pageIndex == PAUSE_ITEM && pauseCtx->cursorYIndex[PAUSE_ITEM] > 3) {
        MyTempCursorYIndex = pauseCtx->cursorYIndex[PAUSE_ITEM];
        pauseCtx->cursorYIndex[PAUSE_ITEM] = 3;
        //sItemMaskCursorsY[3] = 31.0f - (MyTempCursorYIndex*26.0f);
    } else {
        MyTempCursorYIndex = -1;
    }
}

RECOMP_HOOK_RETURN("KaleidoScope_UpdateCursorSize")
void Exit_KaleidoScope_UpdateCursorSize() {
    PauseContext* pauseCtx = &MyTempPlay->pauseCtx;
    if (pauseCtx->cursorSpecialPos == 0 && pauseCtx->pageIndex == PAUSE_ITEM && MyTempCursorYIndex > 3) {
        pauseCtx->cursorYIndex[PAUSE_ITEM] = MyTempCursorYIndex;
        MyTempCursorYIndex = -1;
        //sItemMaskCursorsY[3] = -47.0f; // Row 4
    }
}
