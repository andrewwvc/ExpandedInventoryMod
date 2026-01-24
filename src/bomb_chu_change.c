#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "eztr_api.h"

#include "overlays/actors/ovl_En_Box/z_en_box.h"
#include "overlays/actors/ovl_En_Bom_Chu/z_en_bom_chu.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "overlays/actors/ovl_En_Boom/z_en_boom.h"

#include "object_gi_insect_custom.h"

#include "overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope.h"

#include "z64message.h"
#include "z64player.h"
//#include "assets/objects/gameplay_keep/gameplay_keep.h"

typedef void (*PlayerItemActionInitFunc)(PlayState*, Player*);

s32 Player_UpperAction_CarryActor(Player* this, PlayState* play);
s32 Player_UpperAction_ChangeHeldItem(Player* this, PlayState* play);


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

RECOMP_PATCH void EnBomChu_Move(EnBomChu* this, PlayState* play) {
    CollisionPoly* polySide = NULL;
    CollisionPoly* polyUpDown = NULL;
    s32 bgIdSide;
    s32 bgIdUpDown;
    s32 i;
    s32 isFloorPolyValid;
    f32 lineLength;
    Vec3f posA;
    Vec3f posB;
    Vec3f posSide;
    Vec3f posUpDown;

    bgIdUpDown = bgIdSide = BGCHECK_SCENE;
    isFloorPolyValid = false;

    if (this->actor.params > 0)
        this->movingSpeed = 0.0;
    this->actor.speed = this->movingSpeed;
    lineLength = 2.0f * this->movingSpeed;

    if ((this->timer == 0) || (this->collider.base.acFlags & AC_HIT) || (this->collider.base.ocFlags1 & OC1_HIT)) {
        EnBomChu_Explode(this, play);
        return;
    }

    posA.x = this->actor.world.pos.x + (this->axisUp.x * 2.0f);
    posA.y = this->actor.world.pos.y + (this->axisUp.y * 2.0f);
    posA.z = this->actor.world.pos.z + (this->axisUp.z * 2.0f);

    posB.x = this->actor.world.pos.x - (this->axisUp.x * 4.0f);
    posB.y = this->actor.world.pos.y - (this->axisUp.y * 4.0f);
    posB.z = this->actor.world.pos.z - (this->axisUp.z * 4.0f);

    if (EnBomChu_IsOnCollisionPoly(play, &posA, &posB, &posUpDown, &polyUpDown, &bgIdUpDown)) {
        // forwards
        posB.x = (this->axisForwards.x * lineLength) + posA.x;
        posB.y = (this->axisForwards.y * lineLength) + posA.y;
        posB.z = (this->axisForwards.z * lineLength) + posA.z;

        if (EnBomChu_IsOnCollisionPoly(play, &posA, &posB, &posSide, &polySide, &bgIdSide)) {
            isFloorPolyValid = EnBomChu_UpdateFloorPoly(this, polySide, play);
            Math_Vec3f_Copy(&this->actor.world.pos, &posSide);
            this->actor.floorBgId = bgIdSide;
            this->actor.speed = 0.0f;
        } else {
            if (this->actor.floorPoly != polyUpDown) {
                isFloorPolyValid = EnBomChu_UpdateFloorPoly(this, polyUpDown, play);
            }

            Math_Vec3f_Copy(&this->actor.world.pos, &posUpDown);
            this->actor.floorBgId = bgIdUpDown;
        }
    } else {
        this->actor.speed = 0.0f;
        lineLength *= 3.0f;
        Math_Vec3f_Copy(&posA, &posB);

        for (i = 0; i < 3; i++) {
            if (i == 0) {
                // backwards
                posB.x = posA.x - (this->axisForwards.x * lineLength);
                posB.y = posA.y - (this->axisForwards.y * lineLength);
                posB.z = posA.z - (this->axisForwards.z * lineLength);
            } else if (i == 1) {
                // left
                posB.x = posA.x + (this->axisLeft.x * lineLength);
                posB.y = posA.y + (this->axisLeft.y * lineLength);
                posB.z = posA.z + (this->axisLeft.z * lineLength);
            } else {
                // right
                posB.x = posA.x - (this->axisLeft.x * lineLength);
                posB.y = posA.y - (this->axisLeft.y * lineLength);
                posB.z = posA.z - (this->axisLeft.z * lineLength);
            }

            if (EnBomChu_IsOnCollisionPoly(play, &posA, &posB, &posSide, &polySide, &bgIdSide)) {
                isFloorPolyValid = EnBomChu_UpdateFloorPoly(this, polySide, play);
                Math_Vec3f_Copy(&this->actor.world.pos, &posSide);
                this->actor.floorBgId = bgIdSide;
                break;
            }
        }

        if (i == 3) {
            // no collision nearby
            EnBomChu_Explode(this, play);
        }
    }

    if (isFloorPolyValid) {
        EnBomChu_UpdateRotation(this);
        this->actor.shape.rot.x = -this->actor.world.rot.x;
        this->actor.shape.rot.y = this->actor.world.rot.y;
        this->actor.shape.rot.z = this->actor.world.rot.z;
    }

    if (this->isMoving) {
        Actor_PlaySfx_Flagged2(&this->actor, NA_SE_IT_BOMBCHU_MOVE - SFX_FLAG);
    }

    if (this->actor.speed != 0.0f) {
        this->movingSpeed = this->actor.speed;
    }
}

u64 gLand_mine_icon[] = {
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x37120045140A0019, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x000000002B10004C, 0x140A001900000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x00000000461A0073,
0x7E2D00FF923300E7, 0x3210004C00000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x000000001B0B002E, 0xAF3E00E7C34600FF, 0x511C009900000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x00000000712800A2,
0xC96122FFCF5C17FF, 0xB85918FF4537215C, 0x1E1E143300000000, 0x00000000140A0019,
0x0000000000000000, 0x5D422373B95918E7, 0xD24F06FF9F3900FF, 0x632200C6000000BF,
0x0000003F00000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x00000000652300B2,
0xAF632BFFEACB8AFF, 0xC4B77CFFA6A891FF, 0x3C4B60E51E294EB9, 0xA3440CFF8F3C0AFF,
0x322A3FE2273B59B9, 0xB3B293FFD4C990FF, 0xB8975CFF9A521DFF, 0x5A2000FF000000FF,
0x0000007F00000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000071502AA2,
0xB3A571FF939C95FF, 0x797896FF183F89FF, 0x3E5A87FFB44D0DFF, 0x943C07FF894316FF,
0x77350CFF3F5170FF, 0x274985FF687792FF, 0x818A85FFA2986FFF, 0x715934FF000000FF,
0x000000BF00000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x00000000595D54D0,
0x415F8DFF395798FF, 0x1F4895FF224A96FF, 0xC3A570FF83330AFF, 0x803002FF733008FF,
0x79320CFF8E6B42FF, 0x234279FF14387AFF, 0x113270FF253D67FF, 0x5C5C4BFF000000FF,
0x000000FF00000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000033120345, 0xB3490DE79D3D05E7, 0x753408CCC39259FF,
0x7D8886FF27519EFF, 0x2E57A4FF315AA7FF, 0xC7BF8EFF8F623EFF, 0x762A00FF5E270CFF,
0x7C4A27FFBAAC72FF, 0x244580FF163A7EFF, 0x133676FF465972FF, 0x9F7748FFAF4102FF,
0xB05117E7AB3E03E7, 0x3414006600000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x00000000541D00A2, 0x913502FF95410CFF, 0xA83F03FFB5692CFF,
0xBCB07AFF4D74B9FF, 0x466FBAFF527BC8FF, 0x8895A1FFC2BA89FF, 0xAB925FFF9D7F4CFF,
0xA59A68FF7B7F74FF, 0x1A4087FF183C81FF, 0x33548CFFD1C994FF, 0xC68448FFEC8A44FF,
0xBC5D1FFF813509FF, 0x582304DF0000003F, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x000000002F120071, 0x6C320EFF7B3207FF, 0x893605FFC89B68FF,
0xA3A48CFF6A8FCFFF, 0x79A1EAFF9BC4FBFF, 0x96BFFBFF7B9CD3FF, 0x5D708AFF6A7988FF,
0x607391FF2A5095FF, 0x1C4289FF193D81FF, 0x35568DFFBEBFA2FF, 0xC0A266FF93400EFF,
0x883100FF6C2A04FF, 0x512303FF0B0601FF, 0x000000BF00000000, 0x0000000000000000,
0x0000000000000000, 0x1E0F053393571AFF, 0x673611FF612603FF, 0x8D5628FFB0A572FF,
0x90A3B8FF6E96DFFF, 0xB1D8FFFFE3FDFFFF, 0xCDEFFFFF90B9F7FF, 0x5B83CEFF3860A9FF,
0x2A519AFF21478EFF, 0x1D4287FF1A3E81FF, 0x183B7BFF4E6688FF, 0x9D9770FF916637FF,
0x622603FF773107FF, 0x915116FF1B0F03FF, 0x000000BF00000000, 0x0000000000000000,
0x000000000909001C, 0x824B15E595581BFF, 0x908161FF8C7345FF, 0x8B8258FF788386FF,
0x5278BDFF7BA2E7FF, 0xB8DEFFFFEBFDFFFF, 0xCAF0FFFF89B0F3FF, 0x577FC7FF385FA7FF,
0x2A5097FF21468BFF, 0x1F4387FF1C3F80FF, 0x1A3C7AFF183874FF, 0x4F585CFF7C785DFF,
0x7F754DFF7B6845FF, 0x8F5823FF784410FF, 0x180D03FF0000003F, 0x0000000000000000,
0x0000000044280A7F, 0x995C1FFF925820FF, 0x374A6EFF334258FF, 0x465F84FF34599CFF,
0x456AB0FF6A90D5FF, 0x99BFF8FFB0D7FDFF, 0x99BFFBFF668CD3FF, 0x476DB3FF32589EFF,
0x274C91FF22468AFF, 0x204385FF214281FF, 0x1F407EFF1F3E78FF, 0x233F73FF344766FF,
0x3F4850FF182A47FF, 0x865C3AFF894F15FF, 0x57310BFF000000BF, 0x0000000000000000,
0x000000007C4812E5, 0x9D6023FF8C6549FF, 0x213F79FF173B7DFF, 0x1D4184FF264A8DFF,
0x33579AFF476BAEFF, 0x6488CDFF6A8FD4FF, 0x5E83C7FF476CB1FF, 0x365B9FFF2B4F93FF,
0x234789FF224585FF, 0x234584FF264784FF, 0x2C4B85FF27457CFF, 0x213D72FF183160FF,
0x112751FF0D234BFF, 0x72533FFF8B5117FF, 0x693C0EFF140B02FF, 0x0000007F00000000,
0x2A1806558C5116FF, 0x9F6225FF906544FF, 0x2E4577FF163877FF, 0x1A3D7DFF1F4181FF,
0x4E6892FF7A8A98FF, 0x748BAAFF5070A7FF, 0x395C9EFF315597FF, 0x2A4D8EFF234585FF,
0x214383FF224381FF, 0x294985FF617695FF, 0x717E88FF516583FF, 0x2D4778FF1E3663FF,
0x132951FF0E244CFF, 0x876146FF8E5317FF, 0x70400FFF211204FF, 0x000000FF00000000,
0x241503558C5219FF, 0xA5692BFF93633AFF, 0x495479FF143571FF, 0x173875FF2D4978FF,
0xB39667FFD28C4DFF, 0xCBA970FF9E9D81FF, 0x405D8DFF244584FF, 0x204280FF1D3E7BFF,
0x1D3D79FF23427DFF, 0xA9AE9EFFCBB070FF, 0xAA7947FFB3824BFF, 0x4A5B75FF203660FF,
0x152A53FF1D2D4FFF, 0x8E6343FF874E15FF, 0x673A0EFF2A1805FF, 0x000000FF0000003F,
0x21120355824C15FF, 0xA86D31FFA06326FF, 0x84757BFF123169FF, 0x14336CFF7A7767FF,
0xBA4B0DFFC54700FF, 0x9B3E09FFB68A4FFF, 0x687070FF1A3A74FF, 0x193872FF193771FF,
0x1B3A72FF707C84FF, 0xCAA96EFFD9570CFF, 0xB4460AFF8A380CFF, 0x876E4CFF2E3F5DFF,
0x152B53FF444352FF, 0x98612EFF7F4913FF, 0x653A0EFF2B1805FF, 0x000000FF0000007F,
0x00000000754311FF, 0x9D6329FFB77B3EFF, 0xAB8265FF3F4B6EFF, 0x122E63FF9C5B2CFF,
0xC44F0BFFA84206FF, 0x82360DFF9A5E2AFF, 0x8A8970FF16336BFF, 0x153369FF163368FF,
0x173469FF72776EFF, 0xAF7A41FF983D0BFF, 0xCE6626FFBD4B08FF, 0x7A411BFF2B3C5AFF,
0x303A57FF956743FF, 0x8C5217FF774512FF, 0x58330EFF201204FF, 0x000000FF000000BF,
0x0000000059310CE2, 0x854F19FFBE8449FF, 0xC28449FF9A8179FF, 0x3C4A6EFF5A3D2EFF,
0x723512FF7C350DFF, 0x6C2D0CFF8A572BFF, 0x626356FF112C5EFF, 0x112C5EFF122D5FFF,
0x142E5FFF535D62FF, 0x926D3EFF702D09FF, 0x7D2D00FF782B00FF, 0x763F1BFF192F57FF,
0x816558FF90561EFF, 0x804A14FF643B11FF, 0x502E0CFF1E1003FF, 0x000000FF0000007F,
0x000000002E1A067F, 0x714111FF905B24FF, 0xDBA167FFE0A465FF, 0xDBB491FF6E6965FF,
0x583521FF692F0AFF, 0x914918FF806D44FF, 0x263753FF0E2755FF, 0x0F2856FF0F2754FF,
0x112854FF253650FF, 0x5F563DFF633C1EFF, 0x522408FF763B13FF, 0x625148FF9F7254FF,
0xA35F21FF814B15FF, 0x6F4215FF553310FF, 0x4B2A0AFF000000FF, 0x000000FF0000003F,
0x0000000000000000, 0x482809E26F4011FF, 0x8C5824FFE4B279FF, 0xDBA66AFFD7B593FF,
0xA78358FF7F6E52FF, 0x5C5442FF283547FF, 0x0C234DFF0C234DFF, 0x0D234DFF0D234CFF,
0x0E244BFF0F2349FF, 0x1A2945FF4E4D43FF, 0x70614BFFA58460FF, 0xB07C4BFF8C5218FF,
0x7E4A17FF774B1FFF, 0x5D3A17FF4F2E0CFF, 0x0A0601FF000000FF, 0x000000FF00000000,
0x0000000000000000, 0x180D025F4F2D0AFF, 0x62380EFF814F1EFF, 0xB5814BFFE6B67CFF,
0xE5AD6BFFD69552FF, 0xA3744BFF7E6353FF, 0x53474AFF3E3B47FF, 0x272E45FF333445FF,
0x443C43FF453D44FF, 0x614C44FF866142FF, 0xB77737FFB66F29FF, 0x844E18FF7F4D1BFF,
0x82582DFF664320FF, 0x51300FFF1F1104FF, 0x000000FF000000FF, 0x000000BF00000000,
0x0000000000000000, 0x00000000150B026D, 0x3D2308DF54300BFF, 0x663B10FF7C4C1CFF,
0x9C6833FFBE864DFF, 0xCE8F50FFCF8A48FF, 0xC4894BFFA96D32FF, 0xA56C35FFA16D3FFF,
0x9C632BFFA36E37FF, 0x9D5F22FF9D5D1FFF, 0x94571DFF825120FF, 0x91663CFF835F3AFF,
0x62411FFF4E2E0DFF, 0x321C06FF000000FF, 0x000000FF000000FF, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0E070048311C05DA, 0x412508FF532F0BFF,
0x5D350DFF6A3E12FF, 0x734416FF804E1DFF, 0x875421FF83501DFF, 0x89541FFF87521DFF,
0x7B4A17FF794817FF, 0x764718FF72461AFF, 0x71481FFF6F4A26FF, 0x5C3C1BFF4F3010FF,
0x492A0AFF1D1003FF, 0x000000FF000000FF, 0x000000FF00000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000033, 0x120902D4281704FF,
0x341D06FF422508FF, 0x4F2D0AFF512E0BFF, 0x57320CFF59330CFF, 0x59330DFF5F370EFF,
0x5C350EFF57330EFF, 0x53310FFF4E2E0FFF, 0x472A0CFF482A0BFF, 0x432609FF2E1A06FF,
0x120A02FF000000FF, 0x000000FF000000FF, 0x0000003F00000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000003F070200CC,
0x1C1003FF1F1103FF, 0x271604FF311B06FF, 0x361E06FF392007FF, 0x3A2007FF3B2207FF,
0x3A2107FF3C2208FF, 0x3E2308FF3D2208FF, 0x3D2308FF211304FF, 0x110902FF000000FF,
0x000000FF000000FF, 0x000000FF00000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000003F00000099, 0x070300FF0E0801FF, 0x150C02FF150C02FF, 0x170D02FF1F1103FF,
0x190E03FF120A02FF, 0x1A0E03FF090501FF, 0x000000FF000000FF, 0x000000FF000000FF,
0x000000FF0000003F, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000003F000000BF, 0x000000BF000000FF, 0x000000FF000000FF,
0x000000FF000000FF, 0x000000FF000000FF, 0x000000FF000000FF, 0x0000007F0000003F,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x000000000000003F, 0x0000003F0000003F,
0x0000003F0000007F, 0x0000003F0000003F, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000
};

u64 gLand_mine_item_name_eng[] = {
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001111000000000, 0x0000000000001111,
0x0001111000111100, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001331100000000, 0x0000000000001591,
0x1011331100133111, 0x1000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001BD3100000000, 0x0000000000001BD1,
0x1011DD11011DD1D7, 0x1000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001BD3100011111, 0x1111111101111BD1,
0x1011DD71117DD1DB, 0x1111111111111000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001BD3100113551, 0x1113151111155BD1,
0x1011BDD111DDD133, 0x1131511111331110, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001BD31011DD9DB, 0x11DDDDD313D99DD1,
0x1011B9D3159BD1DB, 0x1DDDDD315D7BD110, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001BD31011711BD, 0x11DD13D71DD11BD1,
0x1011B3D91B3BD1DB, 0x1DD13D73DB11D911, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001BD3101119DDD, 0x11DB13D71D911BD1,
0x1011B1DD3D1BD1DB, 0x1DB13D73DDBBDB11, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001BD31111DB1BD, 0x11DB13D71D911BD1,
0x1011B17DD71BD1DB, 0x1DB13D71DB111111, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001BD31113D51DD, 0x11DB13D71DD11DD1,
0x1013B11DD11BD1DB, 0x1DB13D71BD513711, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001BDDDDD1DDDDD, 0x51DB13D713DDDDD1,
0x1013D117711BD1DB, 0x1DB13D713BDDD311, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001111111113111, 0x1111111111111111,
0x1011111111111111, 0x1111111111111110, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0001111111111111, 0x1111111111111111,
0x0001111111111111, 0x1111111111111100, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000
};

typedef struct GetItemEntry {
    /* 0x0 */ u8 itemId;
    /* 0x1 */ u8 field; // various bit-packed data
    /* 0x2 */ s8 gid;   // defines the draw id and chest opening animation
    /* 0x3 */ u8 textId;
    /* 0x4 */ u16 objectId;
} GetItemEntry; // size = 0x6

#define NewItemNum s16

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

NewItemNum sBombmineIN = -1;


void Player_InitNewExplosiveIA(PlayState* play, Player* this) {
    PlayerExplosive explosiveType;
    Actor* explosiveActor;

    if (this->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
        Player_PutAwayHeldItem(play, this);
        return;
    }

    explosiveType = PLAYER_EXPLOSIVE_BOMBCHU;

    explosiveActor = Actor_SpawnAsChild(&play->actorCtx, &this->actor, play, ACTOR_EN_BOM_CHU,
                                        this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z,
                                        BOMB_EXPLOSIVE_TYPE_BOMB, this->actor.shape.rot.y, 0, 1);//The param should alter the action
    if (explosiveActor != NULL) {
        if ((explosiveType == PLAYER_EXPLOSIVE_BOMBCHU) && (play->unk_1887D != 0)) {
            play->unk_1887D--;
            if (play->unk_1887D == 0) {
                play->unk_1887D = -1;
            }
        } else {
            Inventory_ChangeAmmo(ITEM_BOMBCHU, -1);
        }
        func_8082F5FC(this, explosiveActor);
    } else if (explosiveType == PLAYER_EXPLOSIVE_POWDER_KEG) {
        gSaveContext.powderKegTimer = 0;
    }
}

s32 Player_UpperAction_NewCarryActor(Player* this, PlayState* play) {
    return Player_UpperAction_CarryActor(this, play);
}

RECOMP_PATCH PlayerExplosive Player_ExplosiveFromIA(Player* player, PlayerItemAction itemAction) {
    PlayerExplosive explosive;
    if (itemAction < PLAYER_IA_MAX)
        explosive = GET_EXPLOSIVE_FROM_IA(itemAction);
    else
        explosive = PLAYER_EXPLOSIVE_BOMBCHU;


    if ((explosive > PLAYER_EXPLOSIVE_NONE) && (explosive < PLAYER_EXPLOSIVE_MAX)) {
        return explosive;
    }

    return PLAYER_EXPLOSIVE_NONE;
}

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

void GetItem_DrawMyStuff(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL25_Opa(play->state.gfxCtx);

    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, gGiBugContainerContentsCustomDL);

    Gfx_SetupDL25_Xlu(play->state.gfxCtx);

    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, play->state.gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, gGiBugContainerGlassCustomDL);

    CLOSE_DISPS(play->state.gfxCtx);
}

DrawItemTableEntry customDrawEntry = {
    GetItem_DrawMyStuff,
    { NULL }
};

CustomItemEntry bombmineEntry = {
    .itemName = "BombMine",
    .icon = gLand_mine_icon,
    .nameLabelEng = gLand_mine_item_name_eng,
    .EZTR_KaleidoPopupText = "A bomb with custom behaviour!" EZTR_CC_NEWLINE "Explodes in place, so run" EZTR_CC_NEWLINE "away!" EZTR_CC_END,
    .EZTR_GiveItemText = "A bomb that won't move!" EZTR_CC_NEWLINE "Explodes in place, so have" EZTR_CC_NEWLINE "fun!" EZTR_CC_END,
    .drawEntryGI = &customDrawEntry,//&sDrawItemTable[GID_BUG],
    .mujuraFuncs = {
        .initFunc = Player_InitNewExplosiveIA,
        .actionFunc = Player_UpperAction_NewCarryActor,
        .modelGroup = PLAYER_MODELGROUP_EXPLOSIVES,
        .slotAssignment = SA_AUTO_EMPTY//MAX_REGULAR_SLOTS, //SLOT_LENS_OF_TRUTH,
    },
    .type = IT_EXPLOSIVE,
};

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
    init_items_event();
}

RECOMP_CALLBACK(".", init_items_event)
void init_bombmine() {
    recomp_printf("Callback Called!");
    for (s16 ii = 0; ii < NUM_NEW_INV_SLOTS; ii++) {
        gNewInventoryItemSlots[ii] = ITEM_NONE;
    }
    sBombmineIN = InitializeNewItemFromEntry(&bombmineEntry);
    //gEntryGI = GET_ITEM(ITEM_BOMBCHUS_20, OBJECT_GI_BOMB_2, GID_BOMBCHU, 0x2E, GIFIELD(GIFIELD_40 | GIFIELD_NO_COLLECTIBLE, 0), CHEST_ANIM_SHORT);
    //gEntryGI.itemId = ItemExtension_FromItemRangeToItemID(sBombmineIN);
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
    if (sBombmineIN > -1) {
        if (sNewItemSlotAssignments[sBombmineIN] >= 0 && sNewItemSlotAssignments[sBombmineIN] < SLOT_NONE) {
            if (sNewItemSlotAssignments[sBombmineIN] < MAX_REGULAR_SLOTS)
                gSaveContext.save.saveInfo.inventory.items[sNewItemSlotAssignments[sBombmineIN]] = ItemExtension_FromItemRangeToItemID(sBombmineIN);
            else if (bombmineEntry.mujuraFuncs.slotAssignment == SA_AUTO_PREFILL)
                gNewInventoryItemSlots[sNewItemSlotAssignments[sBombmineIN]-MAX_REGULAR_SLOTS] = ItemExtension_FromItemRangeToItemID(sBombmineIN);
        }

        //SET_CUR_FORM_BTN_ITEM(EQUIP_SLOT_C_LEFT, ItemExtension_FromItemRangeToItemID(sBombmineIN));
        //SET_CUR_FORM_BTN_SLOT(EQUIP_SLOT_C_LEFT, sNewItemSlotAssignments[sBombmineIN]);
        //Interface_LoadItemIconImpl(play, EQUIP_SLOT_C_LEFT);
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
#include "overlays/actors/ovl_En_Guruguru/z_en_guruguru.h"
#include "overlays/actors/ovl_En_Sellnuts/z_en_sellnuts.h"
extern u16 textIDs[];
void func_80BC7520(EnGuruguru* this, PlayState* play);
void func_80ADBCE4(EnSellnuts* this, PlayState* play);

RECOMP_HOOK("EnSellnuts_Init")
void Setup_EnSellnuts_Init(Actor* thisx, PlayState* play){
    EnSellnuts* this = (EnSellnuts*)thisx;
    Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOX, this->actor.world.pos.x+150, this->actor.world.pos.y,
                this->actor.world.pos.z, 0, this->actor.shape.rot.y, SPECIAL_ITEM_CHEST_PARAM | 0x7F,
                ENBOX_PARAMS(ENBOX_TYPE_BIG, 0, 0x12));
}

RECOMP_PATCH
void func_80ADBBEC(EnSellnuts* this, PlayState* play) {
    if (Actor_HasParent(&this->actor, play)) {
        this->actor.parent = NULL;
        SET_WEEKEVENTREG(WEEKEVENTREG_RECEIVED_LAND_TITLE_DEED);
        this->actionFunc = func_80ADBCE4;
    } else {
        ItemExtension_OfferExtendedGetItemUnconditional(&this->actor, play, sBombmineIN);
    }
}

RECOMP_PATCH
void func_80BC7440(EnGuruguru* this, PlayState* play) {
    SkelAnime_Update(&this->skelAnime);
    if (Actor_HasParent(&this->actor, play)) {
        this->actor.parent = NULL;
        this->textIdIndex++;
        this->actor.textId = textIDs[this->textIdIndex];
        Audio_MuteSeqPlayerBgmSub(true);
        Actor_OfferTalkExchange(&this->actor, play, 400.0f, 400.0f, PLAYER_IA_MINUS1);
        this->unk268 = 0;
        SET_WEEKEVENTREG(WEEKEVENTREG_38_40);
        this->actionFunc = func_80BC7520;
    } else {
        ItemExtension_OfferExtendedGetItemFar(&this->actor, play, sBombmineIN);
    }
}
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
