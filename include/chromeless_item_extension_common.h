#ifndef CHROMELESS_ITEM_EXTENSION_COMMON
#define CHROMELESS_ITEM_EXTENSION_COMMON

#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "eztr_api.h"

#include "z64player.h"

#include "overlays/actors/ovl_En_Box/z_en_box.h"

s32 Player_UpperAction_CarryActor(Player* this, PlayState* play);
s32 Player_UpperAction_ChangeHeldItem(Player* this, PlayState* play);


typedef void (*PlayerItemActionInitFunc)(PlayState*, Player*);

typedef s16 NewItemNum;

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

#define SPECIAL_ITEM_CHEST_VALUE 0xFB
#define SPECIAL_ITEM_CHEST_PARAM ((((u16)SPECIAL_ITEM_CHEST_VALUE))<<7)

#define GI_START_TEXT 0xCE
#define GID_CUSTOM GID_4C

//This should be 0x50
#define LAST_REGULAR_ACTION_ITEM ITEM_SWORD_DEITY
#define NEW_ACTION_ITEMS (ITEM_CC+1)
#define NUM_NEW_ITEMS 0x20
#define NEW_ACTION_NUMBERS PLAYER_IA_MAX
#define MESSAGE_ICON_NEW EZTR_ICON_NOTHING_51
#define MAX_REGULAR_SLOTS 0x30
#define NUM_NEW_INV_SLOTS NUM_NEW_ITEMS

#endif
