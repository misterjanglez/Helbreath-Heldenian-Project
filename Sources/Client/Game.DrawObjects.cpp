// Game.DrawObjects.cpp: CGame partial implementation — DrawObjects coordinator + dispatchers
//
//////////////////////////////////////////////////////////////////////

#include "Game.h"
#include "RenderHelpers.h"
#include "EntityRenderState.h"
#include "ConfigManager.h"
#include "GameFonts.h"
#include "TextLibExt.h"
#include "lan_eng.h"

using namespace hb::item;

extern char G_cSpriteAlphaDegree;

// Equipment sprite indices for character rendering (menu only)
struct MenuCharEquipment {
	int body, undies, hair, bodyArmor, armArmor, pants, boots, weapon, shield, mantle, helm;
	int weaponColor, shieldColor, armorColor, mantleColor, armColor, pantsColor, bootsColor, helmColor;
	bool skirtDraw;
};

// Calculate equipment indices for human characters (male/female) in menu
static void CalcHumanEquipment(const CEntityRenderState& state, bool isFemale, MenuCharEquipment& eq)
{
	// Sprite base IDs differ by gender
	int UNDIES  = isFemale ? DEF_SPRID_UNDIES_W    : DEF_SPRID_UNDIES_M;
	int HAIR    = isFemale ? DEF_SPRID_HAIR_W      : DEF_SPRID_HAIR_M;
	int ARMOR   = isFemale ? DEF_SPRID_BODYARMOR_W : DEF_SPRID_BODYARMOR_M;
	int BERK    = isFemale ? DEF_SPRID_BERK_W      : DEF_SPRID_BERK_M;
	int LEGG    = isFemale ? DEF_SPRID_LEGG_W      : DEF_SPRID_LEGG_M;
	int BOOT    = isFemale ? DEF_SPRID_BOOT_W      : DEF_SPRID_BOOT_M;
	int WEAPON  = isFemale ? DEF_SPRID_WEAPON_W    : DEF_SPRID_WEAPON_M;
	int SHIELD  = isFemale ? DEF_SPRID_SHIELD_W    : DEF_SPRID_SHIELD_M;
	int MANTLE  = isFemale ? DEF_SPRID_MANTLE_W    : DEF_SPRID_MANTLE_M;
	int HEAD    = isFemale ? DEF_SPRID_HEAD_W      : DEF_SPRID_HEAD_M;

	// Walking uses pose 3, standing uses pose 2
	bool isWalking = state.m_appearance.iIsWalking != 0;
	int pose = isWalking ? 3 : 2;

	// Read from unpacked appearance
	const auto& appr = state.m_appearance;
	int undiesType   = appr.iUnderwearType;
	int hairType     = appr.iHairStyle;
	int armorType    = appr.iArmorType;
	int armType      = appr.iArmArmorType;
	int pantsType    = appr.iPantsType;
	int helmType     = appr.iHelmType;
	int bootsType    = appr.iBootsType;
	int weaponType   = appr.iWeaponType;
	int shieldType   = appr.iShieldType;
	int mantleType   = appr.iMantleType;
	bool hideArmor   = appr.iHideArmor != 0;

	// Body index
	eq.body = 500 + (state.m_sOwnerType - 1) * 8 * 15 + (pose * 8);

	// Equipment indices (-1 = not equipped)
	eq.undies    = UNDIES + undiesType * 15 + pose;
	eq.hair      = HAIR + hairType * 15 + pose;
	eq.bodyArmor = (!hideArmor && armorType != 0) ? ARMOR + armorType * 15 + pose : -1;
	eq.armArmor  = (armType != 0)   ? BERK + armType * 15 + pose : -1;
	eq.pants     = (pantsType != 0) ? LEGG + pantsType * 15 + pose : -1;
	eq.boots     = (bootsType != 0) ? BOOT + bootsType * 15 + pose : -1;
	eq.mantle    = (mantleType != 0) ? MANTLE + mantleType * 15 + pose : -1;
	eq.helm      = (helmType != 0)  ? HEAD + helmType * 15 + pose : -1;

	// Weapon/shield use direction in frame calculation
	eq.weapon = (weaponType != 0) ? WEAPON + weaponType * 64 + 8 * pose + (state.m_iDir - 1) : -1;
	eq.shield = (shieldType != 0) ? SHIELD + shieldType * 8 + pose : -1;

	// Female skirt check (pants type 1)
	eq.skirtDraw = isFemale && (pantsType == 1);
}

SpriteLib::BoundRect CGame::DrawObject_OnMove_ForMenu(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	// Extract equipment colors from packed appearance color
	MenuCharEquipment eq = {};
	eq.weaponColor = m_entityState.m_appearance.iWeaponColor;
	eq.shieldColor = m_entityState.m_appearance.iShieldColor;
	eq.armorColor  = m_entityState.m_appearance.iArmorColor;
	eq.mantleColor = m_entityState.m_appearance.iMantleColor;
	eq.armColor    = m_entityState.m_appearance.iArmColor;
	eq.pantsColor  = m_entityState.m_appearance.iPantsColor;
	eq.bootsColor  = m_entityState.m_appearance.iBootsColor;
	eq.helmColor   = m_entityState.m_appearance.iHelmColor;

	// Calculate equipment indices based on character type
	bool isMob = false;
	switch (m_entityState.m_sOwnerType) {
	case 1: case 2: case 3:  // Male
		CalcHumanEquipment(m_entityState, false, eq);
		break;
	case 4: case 5: case 6:  // Female
		CalcHumanEquipment(m_entityState, true, eq);
		break;
	default:  // Mob/NPC
		eq.body = DEF_SPRID_MOB + (m_entityState.m_sOwnerType - 10) * 8 * 7 + (1 * 8);
		eq.undies = eq.hair = eq.bodyArmor = eq.armArmor = -1;
		eq.boots = eq.pants = eq.weapon = eq.shield = eq.helm = eq.mantle = -1;
		isMob = true;
		break;
	}
	// Helper lambdas for drawing with optional color tint
	int dirFrame = (m_entityState.m_iDir - 1) * 8 + m_entityState.m_iFrame;
	int hairColor = m_entityState.m_appearance.iHairColor >> 4;
	int iR, iG, iB;

	auto drawEquipment = [&](int idx, int color) {
		if (idx == -1) return;
		if (color == 0)
			m_pSprite[idx]->Draw(sX, sY, dirFrame);
		else
			m_pSprite[idx]->Draw(sX, sY, dirFrame, SpriteLib::DrawParams::Tint(GameColors::Items[color].r - GameColors::Base.r, GameColors::Items[color].g - GameColors::Base.g, GameColors::Items[color].b - GameColors::Base.b));
	};

	auto drawWeapon = [&]() {
		if (eq.weapon == -1) return;
		if (eq.weaponColor == 0)
			m_pSprite[eq.weapon]->Draw(sX, sY, m_entityState.m_iFrame);
		else
			m_pSprite[eq.weapon]->Draw(sX, sY, m_entityState.m_iFrame, SpriteLib::DrawParams::Tint(GameColors::Weapons[eq.weaponColor].r - GameColors::Base.r, GameColors::Weapons[eq.weaponColor].g - GameColors::Base.g, GameColors::Weapons[eq.weaponColor].b - GameColors::Base.b));
	};

	auto drawMantle = [&](int order) {
		if (eq.mantle != -1 && _cMantleDrawingOrder[m_entityState.m_iDir] == order)
			drawEquipment(eq.mantle, eq.mantleColor);
	};

	// Check if mob type should skip shadow
	auto shouldSkipShadow = [&]() {
		switch (m_entityState.m_sOwnerType) {
		case hb::owner::Slime: case hb::owner::EnergySphere: case hb::owner::TigerWorm: case hb::owner::Catapult: case hb::owner::CannibalPlant: case hb::owner::IceGolem: case hb::owner::Abaddon: case hb::owner::Gate:
			return true;
		default:
			return false;
		}
	};

	// Draw body shadow
	if (!shouldSkipShadow() && ConfigManager::Get().GetDetailLevel() != 0 && !isMob)
		m_pSprite[eq.body + (m_entityState.m_iDir - 1)]->Draw(sX, sY, m_entityState.m_iFrame, SpriteLib::DrawParams::Shadow());

	// Draw weapon first if drawing order is 1
	if (_cDrawingOrder[m_entityState.m_iDir] == 1)
		drawWeapon();

	// Draw body
	if (isMob)
		m_pSprite[eq.body + (m_entityState.m_iDir - 1)]->Draw(sX, sY, m_entityState.m_iFrame, SpriteLib::DrawParams::Alpha(0.5f));
	else
		m_pSprite[eq.body + (m_entityState.m_iDir - 1)]->Draw(sX, sY, m_entityState.m_iFrame);

	// Draw equipment layers (back-to-front order)
	drawMantle(0);  // Mantle behind body
	drawEquipment(eq.undies, 0);

	// Hair (only if no helm)
	if (eq.hair != -1 && eq.helm == -1)
	{
		_GetHairColorRGB(hairColor, &iR, &iG, &iB);
		m_pSprite[eq.hair]->Draw(sX, sY, dirFrame, SpriteLib::DrawParams::Tint(iR, iG, iB));
	}

	// Boots before pants if wearing skirt
	if (eq.skirtDraw)
		drawEquipment(eq.boots, eq.bootsColor);

	drawEquipment(eq.pants, eq.pantsColor);
	drawEquipment(eq.armArmor, eq.armColor);

	// Boots after pants if not wearing skirt
	if (!eq.skirtDraw)
		drawEquipment(eq.boots, eq.bootsColor);

	drawEquipment(eq.bodyArmor, eq.armorColor);
	drawEquipment(eq.helm, eq.helmColor);
	drawMantle(2);  // Mantle over armor
	drawEquipment(eq.shield, eq.shieldColor);
	drawMantle(1);  // Mantle in front

	// Draw weapon last if drawing order is not 1
	if (_cDrawingOrder[m_entityState.m_iDir] != 1)
		drawWeapon();

	// Chat message
	if (m_entityState.m_iChatIndex != 0)
	{
		if (m_pChatMsgList[m_entityState.m_iChatIndex] != 0)
			DrawChatMsgBox(sX, sY, m_entityState.m_iChatIndex, false);
		else
			m_pMapData->ClearChatMsg(indexX, indexY);
	}

	m_entityState.m_iMoveOffsetX = 0;
	m_entityState.m_iMoveOffsetY = 0;
	return m_pSprite[eq.body + (m_entityState.m_iDir - 1)]->GetBoundRect();
}

// --- DrawObject dispatcher functions ---

SpriteLib::BoundRect CGame::DrawObject_OnStop(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawStop(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawStop(indexX, indexY, sX, sY, bTrans, dwTime);
}

SpriteLib::BoundRect CGame::DrawObject_OnMove(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawMove(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawMove(indexX, indexY, sX, sY, bTrans, dwTime);
}

SpriteLib::BoundRect CGame::DrawObject_OnRun(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawRun(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawRun(indexX, indexY, sX, sY, bTrans, dwTime);
}

SpriteLib::BoundRect CGame::DrawObject_OnAttack(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawAttack(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawAttack(indexX, indexY, sX, sY, bTrans, dwTime);
}

SpriteLib::BoundRect CGame::DrawObject_OnAttackMove(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawAttackMove(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawAttackMove(indexX, indexY, sX, sY, bTrans, dwTime);
}

SpriteLib::BoundRect CGame::DrawObject_OnMagic(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawMagic(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawMagic(indexX, indexY, sX, sY, bTrans, dwTime);
}

SpriteLib::BoundRect CGame::DrawObject_OnGetItem(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawGetItem(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawGetItem(indexX, indexY, sX, sY, bTrans, dwTime);
}

SpriteLib::BoundRect CGame::DrawObject_OnDamage(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawDamage(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawDamage(indexX, indexY, sX, sY, bTrans, dwTime);
}

SpriteLib::BoundRect CGame::DrawObject_OnDamageMove(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawDamageMove(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawDamageMove(indexX, indexY, sX, sY, bTrans, dwTime);
}

SpriteLib::BoundRect CGame::DrawObject_OnDying(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawDying(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawDying(indexX, indexY, sX, sY, bTrans, dwTime);
}

SpriteLib::BoundRect CGame::DrawObject_OnDead(int indexX, int indexY, int sX, int sY, bool bTrans, uint32_t dwTime)
{
	if (m_entityState.IsPlayer())
		return m_playerRenderer.DrawDead(indexX, indexY, sX, sY, bTrans, dwTime);
	else
		return m_npcRenderer.DrawDead(indexX, indexY, sX, sY, bTrans, dwTime);
}

// --- DrawObjects coordinator ---

void CGame::DrawObjects(short sPivotX, short sPivotY, short sDivX, short sDivY, short sModX, short sModY, short msX, short msY)
{
	int ix, iy, indexX, indexY, dX, dY, iDvalue;
	char cItemColor;
	bool bIsPlayerDrawed = false;
	bool bRet = false;
	short sItemSprite, sItemSpriteFrame, sObjSpr, sObjSprFrame, sDynamicObject, sDynamicObjectFrame;
	char cDynamicObjectData1, cDynamicObjectData2, cDynamicObjectData3, cDynamicObjectData4;
	// Xmas
	static int ix1[100];
	static int iy2[100];
	static int iXmasTreeBulbDelay = 76;
	int idelay = 75;

	// Item's desc on floor
	uint32_t dwItemAttr, dwItemSelectedAttr;
	int iItemSelectedx, iItemSelectedy;
	short sItemID, sItemSelectedID = -1;

	int res_x = LOGICAL_MAX_X();
	int res_y = LOGICAL_MAX_Y();
	int res_msy = LOGICAL_HEIGHT() - 49;

	if (sDivY < 0 || sDivX < 0) return;

	// Initialize Picking system for this frame
	CursorTarget::BeginFrame();

	//dwTime = GameClock::GetTimeMS();
	uint32_t dwTime = m_dwCurTime;

	// Pre-calculate map data bounds for efficient boundary checking
	const short mapMinX = m_pMapData->m_sPivotX;
	const short mapMaxX = m_pMapData->m_sPivotX + MAPDATASIZEX;
	const short mapMinY = m_pMapData->m_sPivotY;
	const short mapMaxY = m_pMapData->m_sPivotY + MAPDATASIZEY;

	// Tile-based loop bounds (much cleaner than pixel-based)
	// Buffer: 7 tiles around visible area for smooth object sliding
	// Extra 2 tiles on bottom for layering/depth sorting of tall objects
	constexpr int TILE_SIZE = 32;
	constexpr int BUFFER_TILES = 7;
	constexpr int EXTRA_BOTTOM_TILES = 2;  // For depth sorting of tall objects

	const int visibleTilesX = (res_x / TILE_SIZE) + 1;  // ~20 tiles
	const int visibleTilesY = (res_y / TILE_SIZE) + 1;  // ~15 tiles

	const int startTileX = -BUFFER_TILES;
	const int endTileX = visibleTilesX + BUFFER_TILES;
	const int startTileY = -BUFFER_TILES;
	const int endTileY = visibleTilesY + BUFFER_TILES + EXTRA_BOTTOM_TILES;

	// Visibility bounds in pixels (for culling non-visible tiles from detailed processing)
	const int visMinX = -sModX;
	const int visMaxX = res_x + 16;
	const int visMinY = -sModY;
	const int visMaxY = res_y + 32 + 16;

	// Loop over tiles, calculate pixel positions when needed
	for (int tileY = startTileY; tileY <= endTileY; tileY++)
	{
		indexY = sDivY + sPivotY + tileY;
		iy = tileY * TILE_SIZE - sModY;

		for (int tileX = startTileX; tileX <= endTileX; tileX++)
		{
			indexX = sDivX + sPivotX + tileX;
			ix = tileX * TILE_SIZE - sModX;

			sDynamicObject = 0;
			bRet = false;
			if ((ix >= visMinX) && (ix <= visMaxX) && (iy >= visMinY) && (iy <= visMaxY))
			{
				m_entityState.m_wObjectID = m_entityState.m_sOwnerType = 0; m_entityState.m_status.Clear();
				m_entityState.m_appearance.Clear();
				m_entityState.m_iDir = m_entityState.m_iFrame = 0;
				m_entityState.m_iEffectType = m_entityState.m_iEffectFrame = m_entityState.m_iChatIndex = 0;
				std::memset(m_entityState.m_cName.data(), 0, m_entityState.m_cName.size());
				if ((indexX < mapMinX) || (indexX > mapMaxX) ||
					(indexY < mapMinY) || (indexY > mapMaxY))
				{
					sItemID = 0;
					bRet = false;
					cItemColor = 0;
					dwItemAttr = 0;
				}
				else
				{
					m_entityState.m_iDataX = dX = indexX - mapMinX;
					m_entityState.m_iDataY = dY = indexY - mapMinY;
					m_entityState.m_wObjectID = m_pMapData->m_pData[dX][dY].m_wDeadObjectID;
					m_entityState.m_sOwnerType = m_pMapData->m_pData[dX][dY].m_sDeadOwnerType;
					m_entityState.m_iDir = m_pMapData->m_pData[dX][dY].m_cDeadDir;
					m_entityState.m_appearance = m_pMapData->m_pData[dX][dY].m_deadAppearance;
					m_entityState.m_iFrame = m_pMapData->m_pData[dX][dY].m_cDeadOwnerFrame;
					m_entityState.m_iChatIndex = m_pMapData->m_pData[dX][dY].m_iDeadChatMsg;
					m_entityState.m_status = m_pMapData->m_pData[dX][dY].m_deadStatus;
					strcpy(m_entityState.m_cName.data(), m_pMapData->m_pData[dX][dY].m_cDeadOwnerName);
					sItemID = m_pMapData->m_pData[dX][dY].m_sItemID;
					dwItemAttr = m_pMapData->m_pData[dX][dY].m_dwItemAttr;
					cItemColor = m_pMapData->m_pData[dX][dY].m_cItemColor;
					sDynamicObject = m_pMapData->m_pData[dX][dY].m_sDynamicObjectType;
					sDynamicObjectFrame = (short)m_pMapData->m_pData[dX][dY].m_cDynamicObjectFrame;
					cDynamicObjectData1 = m_pMapData->m_pData[dX][dY].m_cDynamicObjectData1;
					cDynamicObjectData2 = m_pMapData->m_pData[dX][dY].m_cDynamicObjectData2;
					cDynamicObjectData3 = m_pMapData->m_pData[dX][dY].m_cDynamicObjectData3;
					cDynamicObjectData4 = m_pMapData->m_pData[dX][dY].m_cDynamicObjectData4;

					bRet = true;
				}

				if ((bRet == true) && (sItemID != 0) && m_pItemConfigList[sItemID] != 0)
				{
					if (cItemColor == 0)
						m_pSprite[DEF_SPRID_ITEMGROUND_PIVOTPOINT + m_pItemConfigList[sItemID]->m_sSprite]->Draw(ix, iy, m_pItemConfigList[sItemID]->m_sSpriteFrame);
					else
					{
						switch (m_pItemConfigList[sItemID]->m_sSprite) {
						case 1: // Swds
						case 2: // Bows
						case 3: // Shields
						case hb::owner::ShopKeeper: // Axes hammers
							m_pSprite[DEF_SPRID_ITEMGROUND_PIVOTPOINT + m_pItemConfigList[sItemID]->m_sSprite]->Draw(ix, iy, m_pItemConfigList[sItemID]->m_sSpriteFrame, SpriteLib::DrawParams::Tint(GameColors::Weapons[cItemColor].r - GameColors::Base.r, GameColors::Weapons[cItemColor].g - GameColors::Base.g, GameColors::Weapons[cItemColor].b - GameColors::Base.b));
							break;
						default:
							m_pSprite[DEF_SPRID_ITEMGROUND_PIVOTPOINT + m_pItemConfigList[sItemID]->m_sSprite]->Draw(ix, iy, m_pItemConfigList[sItemID]->m_sSpriteFrame, SpriteLib::DrawParams::Tint(GameColors::Items[cItemColor].r - GameColors::Base.r, GameColors::Items[cItemColor].g - GameColors::Base.g, GameColors::Items[cItemColor].b - GameColors::Base.b));
							break;
						}
					}

					if (Input::IsShiftDown() && msX >= ix - 16 && msY >= iy - 16 && msX <= ix + 16 && msY <= iy + 16) {
						sItemSelectedID = sItemID;
						dwItemSelectedAttr = dwItemAttr;
						iItemSelectedx = ix;
						iItemSelectedy = iy;
					}

					// Test ground item with Picking system
					CursorTarget::TestGroundItem(ix, iy, res_msy);
				}

				if ((bRet == true) && (m_entityState.m_wObjectID != 0))
				{
					SpriteLib::BoundRect bounds = DrawObject_OnDead(indexX, indexY, ix, iy, false, dwTime);

					// Build picking info for dead object
					TargetObjectInfo info = {};
					info.objectID = m_entityState.m_wObjectID;
					info.mapX = indexX;
					info.mapY = indexY;
					info.screenX = ix;
					info.screenY = iy;
					info.dataX = m_entityState.m_iDataX;
					info.dataY = m_entityState.m_iDataY;
					info.ownerType = m_entityState.m_sOwnerType;
					info.action = DEF_OBJECTDEAD;
					info.direction = m_entityState.m_iDir;
					info.frame = m_entityState.m_iFrame;
					info.name = m_entityState.m_cName.data();
					info.appearance = m_entityState.m_appearance;
					info.status = m_entityState.m_status;
					info.type = FocusedObjectType::DeadBody;
					CursorTarget::TestObject(bounds, info, iy, res_msy);
				}

				m_entityState.m_wObjectID = m_entityState.m_sOwnerType = 0; m_entityState.m_status.Clear();
				m_entityState.m_appearance.Clear();
				m_entityState.m_iFrame = m_entityState.m_iDir = 0;
				m_entityState.m_iEffectType = m_entityState.m_iEffectFrame = m_entityState.m_iChatIndex = 0;
				std::memset(m_entityState.m_cName.data(), 0, m_entityState.m_cName.size());

				if ((indexX < mapMinX) || (indexX > mapMaxX) ||
					(indexY < mapMinY) || (indexY > mapMaxY))
				{
					sItemID = 0;
					bRet = false;
				}
				else
				{
					m_entityState.m_iDataX = dX = indexX - mapMinX;
					m_entityState.m_iDataY = dY = indexY - mapMinY;
					m_entityState.m_wObjectID = m_pMapData->m_pData[dX][dY].m_wObjectID;
					m_entityState.m_sOwnerType = m_pMapData->m_pData[dX][dY].m_sOwnerType;
					m_entityState.m_iAction = m_pMapData->m_pData[dX][dY].m_animation.cAction;
					m_entityState.m_status = m_pMapData->m_pData[dX][dY].m_status;
					m_entityState.m_iDir = m_pMapData->m_pData[dX][dY].m_animation.cDir;
					m_entityState.m_appearance = m_pMapData->m_pData[dX][dY].m_appearance;
					m_entityState.m_iFrame = m_pMapData->m_pData[dX][dY].m_animation.cCurrentFrame;
					m_entityState.m_iChatIndex = m_pMapData->m_pData[dX][dY].m_iChatMsg;
					m_entityState.m_iEffectType = m_pMapData->m_pData[dX][dY].m_iEffectType;
					m_entityState.m_iEffectFrame = m_pMapData->m_pData[dX][dY].m_iEffectFrame;

					strcpy(m_entityState.m_cName.data(), m_pMapData->m_pData[dX][dY].m_cOwnerName);
					bRet = true;

					if (m_iIlusionOwnerH != 0)
					{
						if ((strcmp(m_entityState.m_cName.data(), m_pPlayer->m_cPlayerName) != 0) && (!hb::owner::IsNPC(m_entityState.m_sOwnerType)))
						{
							m_entityState.m_sOwnerType = m_cIlusionOwnerType;
							m_entityState.m_status = m_pPlayer->m_illusionStatus;
							m_entityState.m_appearance = m_pPlayer->m_illusionAppearance;
						}
					}
				}

				if ((bRet == true) && (strlen(m_entityState.m_cName.data()) > 0))
				{
					m_entityState.m_iMoveOffsetX = 0;
					m_entityState.m_iMoveOffsetY = 0;
					SpriteLib::BoundRect bounds = {0, -1, 0, 0};
					switch (m_entityState.m_iAction) {
					case DEF_OBJECTSTOP:
						bounds = DrawObject_OnStop(indexX, indexY, ix, iy, false, dwTime);
						break;

					case DEF_OBJECTMOVE:
						bounds = DrawObject_OnMove(indexX, indexY, ix, iy, false, dwTime);
						break;

					case DEF_OBJECTDAMAGEMOVE:
						bounds = DrawObject_OnDamageMove(indexX, indexY, ix, iy, false, dwTime);
						break;

					case DEF_OBJECTRUN:
						bounds = DrawObject_OnRun(indexX, indexY, ix, iy, false, dwTime);
						break;

					case DEF_OBJECTATTACK:
						bounds = DrawObject_OnAttack(indexX, indexY, ix, iy, false, dwTime);
						break;

					case DEF_OBJECTATTACKMOVE:
						bounds = DrawObject_OnAttackMove(indexX, indexY, ix, iy, false, dwTime);
						break;

					case DEF_OBJECTMAGIC:
						bounds = DrawObject_OnMagic(indexX, indexY, ix, iy, false, dwTime);
						break;

					case DEF_OBJECTGETITEM:
						bounds = DrawObject_OnGetItem(indexX, indexY, ix, iy, false, dwTime);
						break;

					case DEF_OBJECTDAMAGE:
						bounds = DrawObject_OnDamage(indexX, indexY, ix, iy, false, dwTime);
						break;

					case DEF_OBJECTDYING:
						bounds = DrawObject_OnDying(indexX, indexY, ix, iy, false, dwTime);
						break;
					}

					// Build picking info for living object
					TargetObjectInfo info = {};
					info.objectID = m_entityState.m_wObjectID;
					info.mapX = indexX;
					info.mapY = indexY;
					info.screenX = ix;
					info.screenY = iy;
					info.dataX = m_entityState.m_iDataX;
					info.dataY = m_entityState.m_iDataY;
					info.ownerType = m_entityState.m_sOwnerType;
					info.action = m_entityState.m_iAction;
					info.direction = m_entityState.m_iDir;
					info.frame = m_entityState.m_iFrame;
					info.name = m_entityState.m_cName.data();
					info.appearance = m_entityState.m_appearance;
					info.status = m_entityState.m_status;
					// Determine type based on owner type
					info.type = (m_entityState.IsPlayer()) ?
						FocusedObjectType::Player : FocusedObjectType::NPC;
					CursorTarget::TestObject(bounds, info, iy, res_msy);

					if (m_entityState.m_wObjectID == m_pPlayer->m_sPlayerObjectID)
					{
						// Camera is now updated in on_render() before drawing, so we don't need to update it here
						// This ensures viewport and entity position use the same motion offset
						m_rcPlayerRect = m_rcBodyRect;
						bIsPlayerDrawed = true;
					}
				}
			}

			// CLEROTH - Object sprites on tiles
			// Bounds check for tile array access (752x752)
			if (indexX >= 0 && indexX < 752 && indexY >= 0 && indexY < 752)
			{
				sObjSpr = m_pMapData->m_tile[indexX][indexY].m_sObjectSprite;
				sObjSprFrame = m_pMapData->m_tile[indexX][indexY].m_sObjectSpriteFrame;
			}
			else
			{
				sObjSpr = 0;
				sObjSprFrame = 0;
			}

			if (sObjSpr != 0)
			{
				if ((sObjSpr < 100) || (sObjSpr >= 200))
				{
					switch (sObjSpr) {
					case 200:
					case 223:
						m_pTileSpr[sObjSpr]->Draw(ix - 16, iy - 16, sObjSprFrame, SpriteLib::DrawParams::Shadow());
						break;

					case 224:
						switch (sObjSprFrame) {
						case hb::owner::Tom:
						case hb::owner::Dummy:
						case hb::owner::EnergySphere:
						case hb::owner::ArrowGuardTower:
						case hb::owner::CannonGuardTower:
						case hb::owner::ManaCollector:
							break;
						default:
							m_pTileSpr[sObjSpr]->Draw(ix - 16, iy - 16, sObjSprFrame, SpriteLib::DrawParams::Shadow());
							break;
						}
					}
					if (ConfigManager::Get().GetDetailLevel() == 0) // Special Grass & Flowers
					{
						if ((sObjSpr != 6) && (sObjSpr != 9))
							m_pTileSpr[sObjSpr]->Draw(ix - 16, iy - 16, sObjSprFrame);
					}
					else
					{
						m_pTileSpr[sObjSpr]->Draw(ix - 16, iy - 16, sObjSprFrame);
					}

					switch (sObjSpr) {
					case 223:
						if (sObjSprFrame == 4)
						{
							if (G_cSpriteAlphaDegree == 2) //nuit
							{
								// Configurable ground lighting parameters
								int baseX = ix;
								int baseY = iy;
								constexpr int LIGHT_RADIUS = 4;           // Radius in tiles
								constexpr float CENTER_INTENSITY = 0.4f;  // Intensity at center (0.0 - 1.0)
								constexpr float EDGE_INTENSITY = 0.1f;    // Intensity at edge (0.0 - 1.0)
								constexpr int TILE_SIZE = 32;             // Pixels per tile

								// Draw ground lights in a circular pattern with distance-based falloff
								for (int ty = -LIGHT_RADIUS; ty <= LIGHT_RADIUS; ty++) {
									for (int tx = -LIGHT_RADIUS; tx <= LIGHT_RADIUS; tx++) {
										// Calculate distance from center (in tiles)
										float distance = sqrtf(static_cast<float>(tx * tx + ty * ty));

										// Skip if outside radius
										if (distance > LIGHT_RADIUS) continue;

										// Calculate intensity based on distance (linear falloff)
										float t = distance / LIGHT_RADIUS;  // 0 at center, 1 at edge
										float intensity = CENTER_INTENSITY * (1.0f - t) + EDGE_INTENSITY * t;

										// Skip very dim lights for performance
										if (intensity < 0.05f) continue;

										// Calculate screen position (isometric offset)
										int lightX = baseX + tx * TILE_SIZE;
										int lightY = baseY + ty * (TILE_SIZE / 2);  // Half for isometric

										// Warm color that gets slightly cooler at edges
										int r = 255;
										int g = static_cast<int>(200 + 30 * (1.0f - t));  // 230 at center, 200 at edge
										int b = static_cast<int>(150 + 30 * (1.0f - t));  // 180 at center, 150 at edge

										m_pEffectSpr[0]->Draw(lightX, lightY, 1,
											SpriteLib::DrawParams::AdditiveColored(r, g, b, intensity));
									}
								}

								// Lamp fixture lights (the actual light sources on the lamp)
								m_pEffectSpr[0]->Draw(ix + 2, iy - 147, 1, SpriteLib::DrawParams::AdditiveColored(255, 230, 180, 0.8f));
								m_pEffectSpr[0]->Draw(ix + 16, iy - 94, 1, SpriteLib::DrawParams::AdditiveColored(255, 230, 180, 0.8f));
								m_pEffectSpr[0]->Draw(ix - 19, iy - 126, 1, SpriteLib::DrawParams::AdditiveColored(255, 230, 180, 0.8f));
							}
						}
						break;

					case 370: // nuit
						if (((dwTime - m_dwEnvEffectTime) > 400) && (sObjSprFrame == 9) && (G_cSpriteAlphaDegree == 2)) m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix - 16 + 30, m_Camera.GetY() + iy - 16 - 334, 0, 0, 0, 0);
						if (((dwTime - m_dwEnvEffectTime) > 400) && (sObjSprFrame == 11) && (G_cSpriteAlphaDegree == 2)) m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix - 16 + 17, m_Camera.GetY() + iy - 16 - 300, 0, 0, 0, 0);
						break;

					case 374: // nuit
						if (((dwTime - m_dwEnvEffectTime) > 400) && (sObjSprFrame == 2) && (G_cSpriteAlphaDegree == 2)) m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix - 7, m_Camera.GetY() + iy - 122, 0, 0, 0, 0);
						if (((dwTime - m_dwEnvEffectTime) > 400) && (sObjSprFrame == 6) && (G_cSpriteAlphaDegree == 2)) m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix - 14, m_Camera.GetY() + iy - 321, 0, 0, 0, 0);
						if (((dwTime - m_dwEnvEffectTime) > 400) && (sObjSprFrame == 7) && (G_cSpriteAlphaDegree == 2)) m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix + 7, m_Camera.GetY() + iy - 356, 0, 0, 0, 0);
						break;

					case 376: // nuit
						if (((dwTime - m_dwEnvEffectTime) > 400) && (sObjSprFrame == 12) && (G_cSpriteAlphaDegree == 2)) {
							m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix - 16, m_Camera.GetY() + iy - 346, 0, 0, 0, 0);
							m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix + 11, m_Camera.GetY() + iy - 308, 0, 0, 0, 0);
						}
						break;

					case 378: // nuit
						if (((dwTime - m_dwEnvEffectTime) > 400) && (sObjSprFrame == 11) && (G_cSpriteAlphaDegree == 2)) m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix, m_Camera.GetY() + iy - 91, 0, 0, 0, 0);
						break;

					case 382: // nuit
						if (((dwTime - m_dwEnvEffectTime) > 400) && (sObjSprFrame == 9) && (G_cSpriteAlphaDegree == 2)) {
							m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix + 73, m_Camera.GetY() + iy - 264, 0, 0, 0, 0);
							m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix + 23, m_Camera.GetY() + iy - 228, 0, 0, 0, 0);
						}
						break;

					case 429:
						if (((dwTime - m_dwEnvEffectTime) > 400) && (sObjSprFrame == 2)) m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, m_Camera.GetX() + ix - 15, m_Camera.GetY() + iy - 224, 0, 0, 0, 0);
						break;
					}
				}
				else // sprites 100..199: Trees and tree shadows
				{
					m_pTileSpr[sObjSpr]->CalculateBounds(ix - 16, iy - 16, sObjSprFrame);
					if (ConfigManager::Get().GetDetailLevel() == 0)
					{
						if (sObjSpr < 100 + 11) m_pTileSpr[100 + 4]->Draw(ix - 16, iy - 16, sObjSprFrame);
						else if (sObjSpr < 100 + 23) m_pTileSpr[100 + 9]->Draw(ix - 16, iy - 16, sObjSprFrame);
						else if (sObjSpr < 100 + 32) m_pTileSpr[100 + 23]->Draw(ix - 16, iy - 16, sObjSprFrame);
						else m_pTileSpr[100 + 32]->Draw(ix - 16, iy - 16, sObjSprFrame);
					}
					else
					{
						if ((bIsPlayerDrawed == true) && (m_pTileSpr[sObjSpr]->GetBoundRect().top <= m_rcPlayerRect.Top()) && (m_pTileSpr[sObjSpr]->GetBoundRect().bottom >= m_rcPlayerRect.Bottom()) &&
							(ConfigManager::Get().GetDetailLevel() >= 2) && (m_pTileSpr[sObjSpr]->GetBoundRect().left <= m_rcPlayerRect.Left()) && (m_pTileSpr[sObjSpr]->GetBoundRect().right >= m_rcPlayerRect.Right()))
						{
							m_pTileSpr[sObjSpr + 50]->Draw(ix, iy, sObjSprFrame, SpriteLib::DrawParams::Fade());
							m_pTileSpr[sObjSpr]->Draw(ix - 16, iy - 16, sObjSprFrame, SpriteLib::DrawParams::Average());
						}
						else
						{
							// Normal rendering - draw shadow and tree opaque (matches original PutSpriteFast)
							m_pTileSpr[sObjSpr + 50]->Draw(ix, iy, sObjSprFrame);
							m_pTileSpr[sObjSpr]->Draw(ix - 16, iy - 16, sObjSprFrame);
						}
						if (m_bIsXmas == true)
						{
							if (G_cSpriteAlphaDegree == 2) // nuit
							{
								if (iXmasTreeBulbDelay < 0 || iXmasTreeBulbDelay > idelay + 1) iXmasTreeBulbDelay = 0;
								if (iXmasTreeBulbDelay > idelay)
								{
									for (int i = 0; i < 100; i++) {
										ix1[i] = 1 * (rand() % 400) - 200;
										iy2[i] = -1 * (rand() % 300);
									}
									iXmasTreeBulbDelay = 0;
								}
								else iXmasTreeBulbDelay++;

								for (int j = 0; j < 100; j++)
								{
									if (m_pTileSpr[sObjSpr]->CheckCollision(ix - 16, iy - 16, sObjSprFrame, ix + ix1[j], iy + iy2[j]))
									{
										m_pEffectSpr[66 + (j % 6)]->Draw(ix + ix1[j], iy + iy2[j], (iXmasTreeBulbDelay >> 2), SpriteLib::DrawParams::Alpha(0.5f));
									}
								}
							}
						}
					}
				}
			}

			// Dynamic Object
			if ((bRet == true) && (sDynamicObject != 0))
			{
				switch (sDynamicObject) {
				case DEF_DYNAMICOBJECT_PCLOUD_BEGIN:	// 10
					if (sDynamicObjectFrame >= 0)
						m_pEffectSpr[23]->Draw(ix + (rand() % 2), iy + (rand() % 2), sDynamicObjectFrame, SpriteLib::DrawParams{0.5f, 0, 0, 0, false});
					break;

				case DEF_DYNAMICOBJECT_PCLOUD_LOOP:		// 11
					m_pEffectSpr[23]->Draw(ix + (rand() % 2), iy + (rand() % 2), sDynamicObjectFrame + 8, SpriteLib::DrawParams{0.5f, 0, 0, 0, false});
					break;

				case DEF_DYNAMICOBJECT_PCLOUD_END:		// 12
					m_pEffectSpr[23]->Draw(ix + (rand() % 2), iy + (rand() % 2), sDynamicObjectFrame + 16, SpriteLib::DrawParams{0.5f, 0, 0, 0, false});
					break;

				case DEF_DYNAMICOBJECT_ICESTORM:		// 8
					iDvalue = (rand() % 5) * (-1);
					m_pEffectSpr[0]->Draw(ix, iy, 1, SpriteLib::DrawParams::TintedAlpha(iDvalue, iDvalue, iDvalue, 0.7f));
					m_pEffectSpr[13]->Draw(ix, iy, sDynamicObjectFrame, SpriteLib::DrawParams{0.7f, 0, 0, 0, false});
					break;

				case DEF_DYNAMICOBJECT_FIRE:			// 1
				case DEF_DYNAMICOBJECT_FIRE3:			// 14
					switch (rand() % 3) {
					case 0: m_pEffectSpr[0]->Draw(ix, iy, 1, SpriteLib::DrawParams{0.25f, 0, 0, 0, false}); break;
					case 1: m_pEffectSpr[0]->Draw(ix, iy, 1, SpriteLib::DrawParams{0.5f, 0, 0, 0, false}); break;
					case 2: m_pEffectSpr[0]->Draw(ix, iy, 1, SpriteLib::DrawParams{0.7f, 0, 0, 0, false}); break;
					}
					m_pEffectSpr[9]->Draw(ix, iy, sDynamicObjectFrame / 3, SpriteLib::DrawParams{0.7f, 0, 0, 0, false});
					break;

				case DEF_DYNAMICOBJECT_FIRE2:			// 13
					switch (rand() % 3) {
					case 0: m_pEffectSpr[0]->Draw(ix, iy, 1, SpriteLib::DrawParams{0.25f, 0, 0, 0, false}); break;
					case 1: m_pEffectSpr[0]->Draw(ix, iy, 1, SpriteLib::DrawParams{0.5f, 0, 0, 0, false}); break;
					case 2: m_pEffectSpr[0]->Draw(ix, iy, 1, SpriteLib::DrawParams{0.7f, 0, 0, 0, false}); break;
					}
					break;

				case DEF_DYNAMICOBJECT_FISH:			// 2
				{
					char cTmpDOdir, cTmpDOframe;
					cTmpDOdir = CMisc::cCalcDirection(cDynamicObjectData1, cDynamicObjectData2, cDynamicObjectData1 + cDynamicObjectData3, cDynamicObjectData2 + cDynamicObjectData4);
					cTmpDOframe = ((cTmpDOdir - 1) * 4) + (rand() % 4);
					m_pSprite[DEF_SPRID_ITEMDYNAMIC_PIVOTPOINT + 0]->Draw(ix + cDynamicObjectData1, iy + cDynamicObjectData2, cTmpDOframe, SpriteLib::DrawParams::Alpha(0.25f));
				}
				break;

				case DEF_DYNAMICOBJECT_MINERAL1:		// 4
					if (ConfigManager::Get().GetDetailLevel() != 0) m_pSprite[DEF_SPRID_ITEMDYNAMIC_PIVOTPOINT + 1]->Draw(ix, iy, 0, SpriteLib::DrawParams::Shadow());
					m_pSprite[DEF_SPRID_ITEMDYNAMIC_PIVOTPOINT + 1]->Draw(ix, iy, 0);
					CursorTarget::TestDynamicObject(m_pSprite[DEF_SPRID_ITEMDYNAMIC_PIVOTPOINT + 1]->GetBoundRect(), indexX, indexY, res_msy);
					break;

				case DEF_DYNAMICOBJECT_MINERAL2:		// 5
					if (ConfigManager::Get().GetDetailLevel() != 0) m_pSprite[DEF_SPRID_ITEMDYNAMIC_PIVOTPOINT + 1]->Draw(ix, iy, 1, SpriteLib::DrawParams::Shadow());
					m_pSprite[DEF_SPRID_ITEMDYNAMIC_PIVOTPOINT + 1]->Draw(ix, iy, 1);
					CursorTarget::TestDynamicObject(m_pSprite[DEF_SPRID_ITEMDYNAMIC_PIVOTPOINT + 1]->GetBoundRect(), indexX, indexY, res_msy);
					break;

				case DEF_DYNAMICOBJECT_SPIKE:			// 9
					m_pEffectSpr[17]->Draw(ix, iy, sDynamicObjectFrame, SpriteLib::DrawParams{0.7f, 0, 0, 0, false});
					break;

				case DEF_DYNAMICOBJECT_ARESDENFLAG1:  // 6
					m_pSprite[DEF_SPRID_ITEMDYNAMIC_PIVOTPOINT + 2]->Draw(ix, iy, sDynamicObjectFrame);
					break;

				case DEF_DYNAMICOBJECT_ELVINEFLAG1: // 7
					m_pSprite[DEF_SPRID_ITEMDYNAMIC_PIVOTPOINT + 2]->Draw(ix, iy, sDynamicObjectFrame);
					break;
				}
			}
		}
	}

	if ((dwTime - m_dwEnvEffectTime) > 400) m_dwEnvEffectTime = dwTime;

	// Finalize Picking system - determines cursor type
	int foeResult = CursorTarget::HasFocusedObject() ? _iGetFOE(CursorTarget::GetFocusStatus()) : 0;
	CursorTarget::EndFrame(foeResult, m_iPointCommandType, m_pPlayer->m_Controller.IsCommandAvailable(), m_bIsGetPointingMode);

	// Update legacy compatibility variables from Picking system
	m_sMCX = CursorTarget::GetFocusedMapX();
	m_sMCY = CursorTarget::GetFocusedMapY();
	std::memset(m_cMCName, 0, sizeof(m_cMCName));
	std::strncpy(m_cMCName, CursorTarget::GetFocusedName(), sizeof(m_cMCName) - 1);

	// Draw focused object with highlight (transparency)
	if (CursorTarget::HasFocusedObject())
	{
		short focusSX, focusSY;
		uint16_t focusObjID;
		short focusOwnerType;
		char focusAction, focusDir, focusFrame;
		PlayerAppearance focusAppearance;
		PlayerStatus focusStatus;
		short focusDataX, focusDataY;

		if (CursorTarget::GetFocusHighlightData(focusSX, focusSY, focusObjID, focusOwnerType,
			focusAction, focusDir, focusFrame, focusAppearance,
			focusStatus, focusDataX, focusDataY))
		{
			// Set up temporary vars for drawing
			m_entityState.m_wObjectID = focusObjID;
			m_entityState.m_sOwnerType = focusOwnerType;
			m_entityState.m_iAction = focusAction;
			m_entityState.m_iFrame = focusFrame;
			m_entityState.m_iDir = focusDir;
			m_entityState.m_appearance = focusAppearance;
			m_entityState.m_status = focusStatus;
			m_entityState.m_iDataX = focusDataX;
			m_entityState.m_iDataY = focusDataY;
			std::memset(m_entityState.m_cName.data(), 0, m_entityState.m_cName.size());
			std::strncpy(m_entityState.m_cName.data(), CursorTarget::GetFocusedName(), m_entityState.m_cName.size() - 1);

			if ((focusAction != DEF_OBJECTDEAD) && (focusFrame < 0)) {
				// Skip drawing invalid frame
			} else {
				switch (focusAction) {
				case DEF_OBJECTSTOP:
					DrawObject_OnStop(m_sMCX, m_sMCY, focusSX, focusSY, true, dwTime);
					break;
				case DEF_OBJECTMOVE:
					switch (focusOwnerType) {
					case 1:
					case 2:
					case 3: // Human M
					case 4:
					case 5:
					case 6: // Human F

					case hb::owner::Troll: // Troll.
					case hb::owner::Ogre: // Ogre
					case hb::owner::Liche: // Liche
					case hb::owner::Demon: // DD
					case hb::owner::Unicorn: // Uni
					case hb::owner::WereWolf: // WW
					case hb::owner::LightWarBeetle: // LWB
					case hb::owner::GodsHandKnight: // GHK
					case hb::owner::GodsHandKnightCK: // GHKABS
					case hb::owner::TempleKnight: // TK
					case hb::owner::BattleGolem: // BG
					case hb::owner::Stalker: // SK
					case hb::owner::HellClaw: // HC
					case hb::owner::TigerWorm: // TW
					case hb::owner::Catapult: // CP
					case hb::owner::Gargoyle: // GG
					case hb::owner::Beholder: // BB
					case hb::owner::DarkElf: // DE
					case hb::owner::Bunny: // Rabbit
					case hb::owner::Cat: // Cat
					case hb::owner::GiantFrog: // Frog
					case hb::owner::MountainGiant: // MG
					case hb::owner::Ettin: // Ettin
					case hb::owner::CannibalPlant: // Plant
					case hb::owner::Rudolph: // Rudolph
					case hb::owner::DireBoar: // DireBoar
					case hb::owner::Frost: // Frost
					case hb::owner::IceGolem: // Ice-Golem
					case hb::owner::Wyvern: // Wyvern
					case hb::owner::Dragon: // Dragon..........Ajouts par Snoopy
					case hb::owner::Centaur: // Centaur
					case hb::owner::ClawTurtle: // ClawTurtle
					case hb::owner::FireWyvern: // FireWyvern
					case hb::owner::GiantCrayfish: // GiantCrayfish
					case hb::owner::GiLizard: // Gi Lizard
					case hb::owner::GiTree: // Gi Tree
					case hb::owner::MasterOrc: // Master Orc
					case hb::owner::Minaus: // Minaus
					case hb::owner::Nizie: // Nizie
					case hb::owner::Tentocle: // Tentocle
					case hb::owner::Abaddon: // Abaddon
					case hb::owner::Sorceress: // Sorceress
					case hb::owner::ATK: // ATK
					case hb::owner::MasterElf: // MasterElf
					case hb::owner::DSK: // DSK
					case hb::owner::HBT: // HBT
					case hb::owner::CT: // CT
					case hb::owner::Barbarian: // Barbarian
					case hb::owner::AGC: // AGC
					case hb::owner::Gate: // Gate
						break;

					default: // 10..27
						m_entityState.m_iFrame = m_entityState.m_iFrame * 2;
						break;
					}

					DrawObject_OnMove(m_sMCX, m_sMCY, focusSX, focusSY, true, dwTime);
					break;

				case DEF_OBJECTDAMAGEMOVE:
					DrawObject_OnDamageMove(m_sMCX, m_sMCY, focusSX, focusSY, true, dwTime);
					break;

				case DEF_OBJECTRUN:
					DrawObject_OnRun(m_sMCX, m_sMCY, focusSX, focusSY, true, dwTime);
					break;

				case DEF_OBJECTATTACK:
					DrawObject_OnAttack(m_sMCX, m_sMCY, focusSX, focusSY, true, dwTime);
					break;

				case DEF_OBJECTATTACKMOVE:
					DrawObject_OnAttackMove(m_sMCX, m_sMCY, focusSX, focusSY, true, dwTime);
					break;

				case DEF_OBJECTMAGIC:
					DrawObject_OnMagic(m_sMCX, m_sMCY, focusSX, focusSY, true, dwTime);
					break;

				case DEF_OBJECTDAMAGE:
					DrawObject_OnDamage(m_sMCX, m_sMCY, focusSX, focusSY, true, dwTime);
					break;

				case DEF_OBJECTDYING:
					DrawObject_OnDying(m_sMCX, m_sMCY, focusSX, focusSY, true, dwTime);
					break;

				case DEF_OBJECTDEAD:
					DrawObject_OnDead(m_sMCX, m_sMCY, focusSX, focusSY, true, dwTime);
					break;
				}
			}
		}
	}

	if (sItemSelectedID != -1) {
		char cStr1[64], cStr2[64], cStr3[64];
		int  iLoc;
		GetItemName(m_pItemConfigList[sItemSelectedID].get(), cStr1, cStr2, cStr3);

		iLoc = 0;
		if (strlen(cStr1) != 0)
		{
			if (m_bIsSpecial)
				TextLib::DrawText(GameFont::Default, msX, msY + 25, cStr1, TextLib::TextStyle::WithShadow(GameColors::UIItemName_Special.r, GameColors::UIItemName_Special.g, GameColors::UIItemName_Special.b));
			else
				TextLib::DrawText(GameFont::Default, msX, msY + 25, cStr1, TextLib::TextStyle::WithShadow(GameColors::UIWhite.r, GameColors::UIWhite.g, GameColors::UIWhite.b));
			iLoc += 15;
		}
		if (strlen(cStr2) != 0)
		{
			TextLib::DrawText(GameFont::Default, msX, msY + 25 + iLoc, cStr2, TextLib::TextStyle::WithShadow(GameColors::UIDisabled.r, GameColors::UIDisabled.g, GameColors::UIDisabled.b));
			iLoc += 15;
		}
		if (strlen(cStr3) != 0)
		{
			TextLib::DrawText(GameFont::Default, msX, msY + 25 + iLoc, cStr3, TextLib::TextStyle::WithShadow(GameColors::UIDisabled.r, GameColors::UIDisabled.g, GameColors::UIDisabled.b));
			iLoc += 15;
		}
	}
}
