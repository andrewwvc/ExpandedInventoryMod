#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "eztr_api.h"

#include "overlays/actors/ovl_En_Bom_Chu/z_en_bom_chu.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "overlays/actors/ovl_En_Boom/z_en_boom.h"

#include "overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope.h"

#include "z64player.h"
//#include "assets/objects/gameplay_keep/gameplay_keep.h"

#define BOMBCHU_SCALE 0.01f

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

#define IDLE_ANIM_NONE 0

//This should be 0x50
#define LAST_REGULAR_ACTION_ITEM ITEM_SWORD_DEITY
#define NEW_ACTION_ITEMS (ITEM_CC+1)
#define ITEM_BOMBMINE NEW_ACTION_ITEMS
#define NUM_NEW_ITEMS 0x20
#define NEW_ACTION_NUMBERS PLAYER_IA_MAX
#define PLAYER_IA_BOMBMINE NEW_ACTION_NUMBERS

typedef void (*PlayerItemActionInitFunc)(PlayState*, Player*);

s32 Player_UpperAction_CarryActor(Player* this, PlayState* play);
s32 Player_UpperAction_ChangeHeldItem(Player* this, PlayState* play);

extern s8 sItemItemActions[];
extern PlayerUpperActionFunc sItemActionUpdateFuncs[PLAYER_IA_MAX];
extern PlayerItemActionInitFunc sItemActionInitFuncs[PLAYER_IA_MAX];
extern u8 sActionModelGroups[PLAYER_IA_MAX];
extern s32 sPlayerUseHeldItem;
extern s32 sPlayerHeldItemButtonIsHeldDown;
extern PlayerAnimationHeader* D_8085BE84[PLAYER_ANIMGROUP_MAX][PLAYER_ANIMTYPE_MAX];

// This function can be named whatever you want.
EZTR_ON_INIT void ETZR_Item_Expansion_function() {
    EZTR_Basic_ReplaceText(
        (0x1700+ITEM_BOMBMINE),
        EZTR_STANDARD_TEXT_BOX_II,
        1,
        EZTR_ICON_BOMBCHU,
        EZTR_NO_VALUE,
        EZTR_NO_VALUE,
        EZTR_NO_VALUE,
        true,
        "A bomb with custom behaviour!" EZTR_CC_NEWLINE "Explodes in place, so run" EZTR_CC_NEWLINE "away!" EZTR_CC_END,
        NULL
    );
}

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

RECOMP_PATCH PlayerExplosive Player_ExplosiveFromIA(Player* player, PlayerItemAction itemAction) {
    PlayerExplosive explosive;
    if (itemAction == PLAYER_IA_BOMBMINE)
        explosive = PLAYER_EXPLOSIVE_BOMBCHU;
    else
        explosive = GET_EXPLOSIVE_FROM_IA(itemAction);

    if ((explosive > PLAYER_EXPLOSIVE_NONE) && (explosive < PLAYER_EXPLOSIVE_MAX)) {
        return explosive;
    }

    return PLAYER_EXPLOSIVE_NONE;
}

RECOMP_PATCH PlayerModelGroup Player_ActionToModelGroup(Player* player, PlayerItemAction itemAction) {
    PlayerModelGroup modelGroup;
    if (itemAction == PLAYER_IA_BOMBMINE)
        modelGroup = PLAYER_MODELGROUP_EXPLOSIVES;
    else
        modelGroup = sActionModelGroups[itemAction];

    if ((modelGroup == PLAYER_MODELGROUP_ONE_HAND_SWORD) && Player_IsGoronOrDeku(player)) {
        return PLAYER_MODELGROUP_1;
    }
    return modelGroup;
}

s8 gNewItemActions[NUM_NEW_ITEMS] = {PLAYER_IA_BOMBMINE};
PlayerItemActionInitFunc gNewItemActionInitFuncs[NUM_NEW_ITEMS] = {Player_InitNewExplosiveIA};
PlayerUpperActionFunc gNewItemActionUpdateFuncs[NUM_NEW_ITEMS] = {};//Use Player_UpperAction_CarryActor
TexturePtr gNewItemIcons[NUM_NEW_ITEMS];
TexturePtr gNewItemNames[NUM_NEW_ITEMS];

s16 ItemExtension_ToNewItemRange(s16 itemID) {
    return itemID - NEW_ACTION_ITEMS;
}

RECOMP_CALLBACK("*", recomp_on_init)
void on_init() {
    recomp_printf("Callback Called!");
    gNewItemIcons[0] = gLand_mine_icon;
    gNewItemNames[0] = gLand_mine_item_name_eng;
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
    gNewItemActionUpdateFuncs[0] = Player_UpperAction_CarryActor;
    //Player_SetUpperAction(play, this, this->heldItemAction<NEW_ACTION_NUMBERS ? sItemActionUpdateFuncs[this->heldItemAction] : gNewItemActionUpdateFuncs[this->heldItemAction-NEW_ACTION_NUMBERS]);
    recomp_printf("getHeldItemAction: %d\n", this->heldItemAction);
    Player_SetUpperAction(play, this, (this->heldItemAction < NEW_ACTION_NUMBERS) ?
                    sItemActionUpdateFuncs[this->heldItemAction] :
                    gNewItemActionUpdateFuncs[this->heldItemAction-NEW_ACTION_NUMBERS]);
}

RECOMP_PATCH void func_808309CC(PlayState* play, Player* this) {
    if (Player_UpperAction_ChangeHeldItem == this->upperActionFunc) {
        Player_FinishItemChange(play, this);
    }

    //Player_SetUpperAction(play, this, this->heldItemAction<NEW_ACTION_NUMBERS ? sItemActionUpdateFuncs[this->heldItemAction] : gNewItemActionUpdateFuncs[this->heldItemAction-NEW_ACTION_NUMBERS]);
    newSetUpper(play,this);
    this->unk_ACC = 0;
    this->idleType = PLAYER_IDLE_DEFAULT;
    Player_DetachHeldActor(play, this);
    this->stateFlags3 &= ~PLAYER_STATE3_START_CHANGING_HELD_ITEM;
}

RECOMP_PATCH s32 Player_UpperAction_ChangeHeldItem(Player* this, PlayState* play) {
    //Inventory_UnequipItem(ITEM_PICTOGRAPH_BOX);
    INV_CONTENT(ITEM_PICTOGRAPH_BOX) = ITEM_BOMBMINE;
    // for (i = EQUIP_SLOT_C_LEFT; i <= EQUIP_SLOT_C_RIGHT; i++) {
    //     if (GET_CUR_FORM_BTN_ITEM(i) == ITEM_PICTOGRAPH_BOX) {
    //         SET_CUR_FORM_BTN_ITEM(i, ITEM_BOMBMINE);
    //         Interface_LoadItemIconImpl(play, i);
    //         break;
    //     }
    // }

    if (PlayerAnimation_Update(play, &this->skelAnimeUpper) ||
        ((Player_ItemToItemAction(this, this->heldItemId) == this->heldItemAction) &&
         (sPlayerUseHeldItem = (sPlayerUseHeldItem || ((this->modelAnimType != PLAYER_ANIMTYPE_3) &&
                                                       (this->heldItemAction != PLAYER_IA_DEKU_STICK) &&
                                                       (play->bButtonAmmoPlusOne == 0)))))) {
        //Player_SetUpperAction(play, this, this->heldItemAction<NEW_ACTION_NUMBERS ? sItemActionUpdateFuncs[this->heldItemAction] : gNewItemActionUpdateFuncs[this->heldItemAction-NEW_ACTION_NUMBERS]);
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
        //Player_SetUpperAction(play, this, this->heldItemAction<NEW_ACTION_NUMBERS ? sItemActionUpdateFuncs[this->heldItemAction] : gNewItemActionUpdateFuncs[this->heldItemAction-NEW_ACTION_NUMBERS]);
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
extern u8 gItemIconMoonsTearTex[];

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
            recomp_printf("Interface_DrawPauseMenuEquippingIcons - New Item\n");
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
    for (i = 0, j = ITEM_NUM_SLOTS * 4; i < 3; i++, j += 4) {
        if (GET_CUR_FORM_BTN_ITEM(i + 1) != ITEM_NONE) {
            if (GET_CUR_FORM_BTN_SLOT(i + 1) < ITEM_NUM_SLOTS) {
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
                //recomp_printf("KaleidoScope_DrawItemSelect - New Item\n");
                KaleidoScope_DrawTexQuadRGBA32(
                    play->state.gfxCtx, gNewItemIcons[ItemExtension_ToNewItemRange(gSaveContext.save.saveInfo.inventory.items[i])], 32, 32, 0);
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

// extern s16 sEquipState;
// extern s16 sEquipMagicArrowSlotHoldTimer;
// extern s16 sEquipAnimTimer;
// extern s16 sCButtonPosX[];
// extern s16 sCButtonPosY[];
//
// RECOMP_PATCH void KaleidoScope_UpdateItemEquip(PlayState* play) {
//     static s16 sEquipMagicArrowBowSlotHoldTimer = 0;
//     PauseContext* pauseCtx = &play->pauseCtx;
//     Vtx* bowItemVtx;
//     u16 offsetX;
//     u16 offsetY;
//
//     // Grow glowing orb when equipping magic arrows
//     if (sEquipState == EQUIP_STATE_MAGIC_ARROW_GROW_ORB) {
//         pauseCtx->equipAnimAlpha += 14;
//         if (pauseCtx->equipAnimAlpha > 255) {
//             pauseCtx->equipAnimAlpha = 254;
//             sEquipState++;
//         }
//         // Hover over magic arrow slot when the next state is reached
//         sEquipMagicArrowSlotHoldTimer = 5;
//         return;
//     }
//
//     if (sEquipState == EQUIP_STATE_MAGIC_ARROW_HOVER_OVER_BOW_SLOT) {
//         sEquipMagicArrowBowSlotHoldTimer--;
//
//         if (sEquipMagicArrowBowSlotHoldTimer == 0) {
//             pauseCtx->equipTargetItem -= 0xB5 - ITEM_BOW_FIRE;
//             pauseCtx->equipTargetSlot = SLOT_BOW;
//             sEquipAnimTimer = 6;
//             pauseCtx->equipAnimScale = 320;
//             pauseCtx->equipAnimShrinkRate = 40;
//             sEquipState++;
//             Audio_PlaySfx(NA_SE_SY_SYNTH_MAGIC_ARROW);
//         }
//         return;
//     }
//
//     if (sEquipState == EQUIP_STATE_MAGIC_ARROW_MOVE_TO_BOW_SLOT) {
//         bowItemVtx = &pauseCtx->itemVtx[SLOT_BOW * 4];
//         offsetX = ABS_ALT(pauseCtx->equipAnimX - bowItemVtx->v.ob[0] * 10) / sEquipAnimTimer;
//         offsetY = ABS_ALT(pauseCtx->equipAnimY - bowItemVtx->v.ob[1] * 10) / sEquipAnimTimer;
//     } else {
//         offsetX = ABS_ALT(pauseCtx->equipAnimX - sCButtonPosX[pauseCtx->equipTargetCBtn]) / sEquipAnimTimer;
//         offsetY = ABS_ALT(pauseCtx->equipAnimY - sCButtonPosY[pauseCtx->equipTargetCBtn]) / sEquipAnimTimer;
//     }
//
//     if ((pauseCtx->equipTargetItem >= 0xB5 && pauseCtx->equipTargetItem < NEW_ACTION_ITEMS) && (pauseCtx->equipAnimAlpha < 254)) {
//         pauseCtx->equipAnimAlpha += 14;
//         if (pauseCtx->equipAnimAlpha > 255) {
//             pauseCtx->equipAnimAlpha = 254;
//         }
//         sEquipMagicArrowSlotHoldTimer = 5;
//         return;
//     }
//
//     if (sEquipMagicArrowSlotHoldTimer == 0) {
//         pauseCtx->equipAnimScale -= pauseCtx->equipAnimShrinkRate / sEquipAnimTimer;
//         pauseCtx->equipAnimShrinkRate -= pauseCtx->equipAnimShrinkRate / sEquipAnimTimer;
//
//         // Update coordinates of item icon while being equipped
//         if (sEquipState == EQUIP_STATE_MAGIC_ARROW_MOVE_TO_BOW_SLOT) {
//             // target is the bow slot
//             if (pauseCtx->equipAnimX >= (pauseCtx->itemVtx[SLOT_BOW * 4].v.ob[0] * 10)) {
//                 pauseCtx->equipAnimX -= offsetX;
//             } else {
//                 pauseCtx->equipAnimX += offsetX;
//             }
//
//             if (pauseCtx->equipAnimY >= (pauseCtx->itemVtx[SLOT_BOW * 4].v.ob[1] * 10)) {
//                 pauseCtx->equipAnimY -= offsetY;
//             } else {
//                 pauseCtx->equipAnimY += offsetY;
//             }
//         } else {
//             // target is the c button
//             if (pauseCtx->equipAnimX >= sCButtonPosX[pauseCtx->equipTargetCBtn]) {
//                 pauseCtx->equipAnimX -= offsetX;
//             } else {
//                 pauseCtx->equipAnimX += offsetX;
//             }
//
//             if (pauseCtx->equipAnimY >= sCButtonPosY[pauseCtx->equipTargetCBtn]) {
//                 pauseCtx->equipAnimY -= offsetY;
//             } else {
//                 pauseCtx->equipAnimY += offsetY;
//             }
//         }
//
//         sEquipAnimTimer--;
//         if (sEquipAnimTimer == 0) {
//             if (sEquipState == EQUIP_STATE_MAGIC_ARROW_MOVE_TO_BOW_SLOT) {
//                 sEquipState++;
//                 sEquipMagicArrowBowSlotHoldTimer = 4;
//                 return;
//             }
//
//             // Equip item onto c buttons
//             if (pauseCtx->equipTargetCBtn == PAUSE_EQUIP_C_LEFT) {
//                 // Swap if item is already equipped on CDown or CRight.
//                 if (pauseCtx->equipTargetSlot == C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN)) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) & 0xFF) != ITEM_NONE) {
//                         if ((pauseCtx->equipTargetItem >= 0xB5) && (pauseCtx->equipTargetItem < 0xB8) &&
//                             (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) & 0xFF) == ITEM_BOW) ||
//                              (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) & 0xFF) >= ITEM_BOW_FIRE) &&
//                               ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) & 0xFF) <= ITEM_BOW_LIGHT)))) {
//                             pauseCtx->equipTargetItem -= 0xB5 - ITEM_BOW_FIRE;
//                             pauseCtx->equipTargetSlot = SLOT_BOW;
//                         } else {
//                             BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT);
//                             C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN) = C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT);
//                             Interface_LoadItemIcon(play, EQUIP_SLOT_C_DOWN);
//                         }
//                     } else {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) = ITEM_NONE;
//                         C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN) = SLOT_NONE;
//                     }
//                 } else if (pauseCtx->equipTargetSlot == C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT)) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) & 0xFF) != ITEM_NONE) {
//                         if ((pauseCtx->equipTargetItem >= 0xB5) && (pauseCtx->equipTargetItem < 0xB8) &&
//                             (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) & 0xFF) == ITEM_BOW) ||
//                              (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) & 0xFF) >= ITEM_BOW_FIRE) &&
//                               ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) & 0xFF) <= ITEM_BOW_LIGHT)))) {
//                             pauseCtx->equipTargetItem -= 0xB5 - ITEM_BOW_FIRE;
//                             pauseCtx->equipTargetSlot = SLOT_BOW;
//                         } else {
//                             BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT);
//                             C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT) = C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT);
//                             Interface_LoadItemIcon(play, EQUIP_SLOT_C_RIGHT);
//                         }
//                     } else {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) = ITEM_NONE;
//                         C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT) = SLOT_NONE;
//                     }
//                 }
//
//                 // Special case for magic arrows
//                 if ((pauseCtx->equipTargetItem >= 0xB5) && (pauseCtx->equipTargetItem < 0xB8)) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) == ITEM_BOW) ||
//                         ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) >= ITEM_BOW_FIRE) &&
//                          (BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) <= ITEM_BOW_LIGHT))) {
//                         pauseCtx->equipTargetItem -= 0xB5 - ITEM_BOW_FIRE;
//                         pauseCtx->equipTargetSlot = SLOT_BOW;
//                     }
//                 } else if (pauseCtx->equipTargetItem == ITEM_BOW) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) >= ITEM_BOW_FIRE) &&
//                         (BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) <= ITEM_BOW_LIGHT)) {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT);
//                         C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN) = C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT);
//                         Interface_LoadItemIcon(play, EQUIP_SLOT_C_DOWN);
//                     } else if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) >= ITEM_BOW_FIRE) &&
//                                (BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) <= ITEM_BOW_LIGHT)) {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT);
//                         C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT) = C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT);
//                         Interface_LoadItemIcon(play, EQUIP_SLOT_C_RIGHT);
//                     }
//                 }
//
//                 // Equip item on CLeft
//                 BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) = pauseCtx->equipTargetItem;
//                 C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT) = pauseCtx->equipTargetSlot;
//                 Interface_LoadItemIconImpl(play, EQUIP_SLOT_C_LEFT);
//             } else if (pauseCtx->equipTargetCBtn == PAUSE_EQUIP_C_DOWN) {
//                 // Swap if item is already equipped on CLeft or CRight.
//                 if (pauseCtx->equipTargetSlot == C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT)) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) & 0xFF) != ITEM_NONE) {
//                         if ((pauseCtx->equipTargetItem >= 0xB5) && (pauseCtx->equipTargetItem < 0xB8) &&
//                             (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) & 0xFF) == ITEM_BOW) ||
//                              (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) & 0xFF) >= ITEM_BOW_FIRE) &&
//                               ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) & 0xFF) <= ITEM_BOW_LIGHT)))) {
//                             pauseCtx->equipTargetItem -= 0xB5 - ITEM_BOW_FIRE;
//                             pauseCtx->equipTargetSlot = SLOT_BOW;
//                         } else {
//                             BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN);
//                             C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT) = C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN);
//                             Interface_LoadItemIcon(play, EQUIP_SLOT_C_LEFT);
//                         }
//                     } else {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) = ITEM_NONE;
//                         C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT) = SLOT_NONE;
//                     }
//                 } else if (pauseCtx->equipTargetSlot == C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT)) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) & 0xFF) != ITEM_NONE) {
//                         if ((pauseCtx->equipTargetItem >= 0xB5) && (pauseCtx->equipTargetItem < 0xB8) &&
//                             (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) & 0xFF) == ITEM_BOW) ||
//                              (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) & 0xFF) >= ITEM_BOW_FIRE) &&
//                               ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) & 0xFF) <= ITEM_BOW_LIGHT)))) {
//                             pauseCtx->equipTargetItem -= 0xB5 - ITEM_BOW_FIRE;
//                             pauseCtx->equipTargetSlot = SLOT_BOW;
//                         } else {
//                             BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN);
//                             C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT) = C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN);
//                             Interface_LoadItemIcon(play, EQUIP_SLOT_C_RIGHT);
//                         }
//                     } else {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) = ITEM_NONE;
//                         C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT) = SLOT_NONE;
//                     }
//                 }
//
//                 // Special case for magic arrows
//                 if ((pauseCtx->equipTargetItem >= 0xB5) && (pauseCtx->equipTargetItem < 0xB8)) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) == ITEM_BOW) ||
//                         ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) >= ITEM_BOW_FIRE) &&
//                          (BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) <= ITEM_BOW_LIGHT))) {
//                         pauseCtx->equipTargetItem -= 0xB5 - ITEM_BOW_FIRE;
//                         pauseCtx->equipTargetSlot = SLOT_BOW;
//                     }
//                 } else if (pauseCtx->equipTargetItem == ITEM_BOW) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) >= ITEM_BOW_FIRE) &&
//                         (BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) <= ITEM_BOW_LIGHT)) {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN);
//                         Interface_LoadItemIcon(play, EQUIP_SLOT_C_LEFT);
//                     } else if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) >= ITEM_BOW_FIRE) &&
//                                (BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) <= ITEM_BOW_LIGHT)) {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN);
//                         Interface_LoadItemIcon(play, EQUIP_SLOT_C_RIGHT);
//                     }
//                 }
//
//                 // Equip item on CDown
//                 BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) = pauseCtx->equipTargetItem;
//                 C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN) = pauseCtx->equipTargetSlot;
//                 Interface_LoadItemIconImpl(play, EQUIP_SLOT_C_DOWN);
//             } else { // (pauseCtx->equipTargetCBtn == PAUSE_EQUIP_C_RIGHT)
//                 // Swap if item is already equipped on CLeft or CDown.
//                 if (pauseCtx->equipTargetSlot == C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT)) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) & 0xFF) != ITEM_NONE) {
//                         if ((pauseCtx->equipTargetItem >= 0xB5) && (pauseCtx->equipTargetItem < 0xB8) &&
//                             (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) & 0xFF) == ITEM_BOW) ||
//                              (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) & 0xFF) >= ITEM_BOW_FIRE) &&
//                               ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) & 0xFF) <= ITEM_BOW_LIGHT)))) {
//                             pauseCtx->equipTargetItem -= 0xB5 - ITEM_BOW_FIRE;
//                             pauseCtx->equipTargetSlot = SLOT_BOW;
//                         } else {
//                             BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT);
//                             C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT) = C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT);
//                             Interface_LoadItemIcon(play, EQUIP_SLOT_C_LEFT);
//                         }
//                     } else {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) = ITEM_NONE;
//                         C_SLOT_EQUIP(0, EQUIP_SLOT_C_LEFT) = SLOT_NONE;
//                     }
//                 } else if (pauseCtx->equipTargetSlot == C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN)) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) & 0xFF) != ITEM_NONE) {
//                         if ((pauseCtx->equipTargetItem >= 0xB5) && (pauseCtx->equipTargetItem < 0xB8) &&
//                             (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) & 0xFF) == ITEM_BOW) ||
//                              (((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) & 0xFF) >= ITEM_BOW_FIRE) &&
//                               ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) & 0xFF) <= ITEM_BOW_LIGHT)))) {
//                             pauseCtx->equipTargetItem -= 0xB5 - ITEM_BOW_FIRE;
//                             pauseCtx->equipTargetSlot = SLOT_BOW;
//                         } else {
//                             BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT);
//                             C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN) = C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT);
//                             Interface_LoadItemIcon(play, EQUIP_SLOT_C_DOWN);
//                         }
//                     } else {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) = ITEM_NONE;
//                         C_SLOT_EQUIP(0, EQUIP_SLOT_C_DOWN) = SLOT_NONE;
//                     }
//                 }
//
//                 // Special case for magic arrows
//                 if ((pauseCtx->equipTargetItem >= 0xB5) && (pauseCtx->equipTargetItem < 0xB8)) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) == ITEM_BOW) ||
//                         ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) >= ITEM_BOW_FIRE) &&
//                          (BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) <= ITEM_BOW_LIGHT))) {
//                         pauseCtx->equipTargetItem -= 0xB5 - ITEM_BOW_FIRE;
//                         pauseCtx->equipTargetSlot = SLOT_BOW;
//                     }
//                 } else if (pauseCtx->equipTargetItem == ITEM_BOW) {
//                     if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) >= ITEM_BOW_FIRE) &&
//                         (BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) <= ITEM_BOW_LIGHT)) {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT);
//                         Interface_LoadItemIcon(play, EQUIP_SLOT_C_LEFT);
//                     } else if ((BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) >= ITEM_BOW_FIRE) &&
//                                (BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) <= ITEM_BOW_LIGHT)) {
//                         BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_DOWN) = BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT);
//                         Interface_LoadItemIcon(play, EQUIP_SLOT_C_DOWN);
//                     }
//                 }
//
//                 // Equip item on CRight
//                 BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_RIGHT) = pauseCtx->equipTargetItem;
//                 C_SLOT_EQUIP(0, EQUIP_SLOT_C_RIGHT) = pauseCtx->equipTargetSlot;
//                 Interface_LoadItemIconImpl(play, EQUIP_SLOT_C_RIGHT);
//             }
//
//             // Reset params
//             pauseCtx->mainState = PAUSE_MAIN_STATE_IDLE;
//             sEquipAnimTimer = 10;
//             pauseCtx->equipAnimScale = 320;
//             pauseCtx->equipAnimShrinkRate = 40;
//         }
//     } else {
//         sEquipMagicArrowSlotHoldTimer--;
//         if (sEquipMagicArrowSlotHoldTimer == 0) {
//             pauseCtx->equipAnimAlpha = 255;
//         }
//     }
// }
