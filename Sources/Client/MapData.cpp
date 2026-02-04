// MapData.cpp: implementation of the CMapData class.
//
//////////////////////////////////////////////////////////////////////
#define _WINSOCKAPI_

#include "MapData.h"
#include "OwnerType.h"
#include "ObjectIDRange.h"
#include "CommonTypes.h"
#include "StatusFlags.h"
#include "Benchmark.h"
#include "EntityMotion.h"
#include <cstring>
#include <cstdio>

namespace
{
	const uint32_t DEF_FULLDATA_REQUEST_INTERVAL = 2000;
	uint32_t g_dwLastFullDataRequestTime[30000];
	bool ShouldRequestFullData(uint16_t wObjectID, int sX, int sY)
	{
		if (hb::objectid::IsNearbyOffset(wObjectID)) return false;
		if (sX != -1 || sY != -1) return true;

		uint32_t dwNow = GameClock::GetTimeMS();
		if (dwNow - g_dwLastFullDataRequestTime[wObjectID] < DEF_FULLDATA_REQUEST_INTERVAL) {
			return false;
		}
		g_dwLastFullDataRequestTime[wObjectID] = dwNow;
		return true;
	}
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CMapData::CMapData(class CGame* pGame)
{
	int i;
	m_pGame = pGame;
	std::memset(m_iObjectIDcacheLocX, 0, sizeof(m_iObjectIDcacheLocX));
	std::memset(m_iObjectIDcacheLocY, 0, sizeof(m_iObjectIDcacheLocY));
	m_dwDOframeTime = m_dwFrameTime = GameClock::GetTimeMS();

	for (i = 0; i < DEF_TOTALCHARACTERS; i++)
	{
		m_stFrame[i][DEF_OBJECTMOVE].m_sMaxFrame = 7;
	}
	for (i = 1; i <= 6; i++)
	{
		// Original Helbreath 3.82 timing values
		m_stFrame[i][DEF_OBJECTSTOP].m_sMaxFrame = 14;
		m_stFrame[i][DEF_OBJECTSTOP].m_sFrameTime = 60;
		m_stFrame[i][DEF_OBJECTMOVE].m_sMaxFrame = 7;
		m_stFrame[i][DEF_OBJECTMOVE].m_sFrameTime = 74;
		m_stFrame[i][DEF_OBJECTDAMAGEMOVE].m_sMaxFrame = 3;
		m_stFrame[i][DEF_OBJECTDAMAGEMOVE].m_sFrameTime = 50;
		m_stFrame[i][DEF_OBJECTRUN].m_sMaxFrame = 7;
		m_stFrame[i][DEF_OBJECTRUN].m_sFrameTime = 39;
		m_stFrame[i][DEF_OBJECTATTACK].m_sMaxFrame = 7;
		m_stFrame[i][DEF_OBJECTATTACK].m_sFrameTime = 78;
		m_stFrame[i][DEF_OBJECTATTACKMOVE].m_sMaxFrame = 12;
		m_stFrame[i][DEF_OBJECTATTACKMOVE].m_sFrameTime = 78;
		m_stFrame[i][DEF_OBJECTMAGIC].m_sMaxFrame = 15;
		m_stFrame[i][DEF_OBJECTMAGIC].m_sFrameTime = 88;
		m_stFrame[i][DEF_OBJECTGETITEM].m_sMaxFrame = 3;
		m_stFrame[i][DEF_OBJECTGETITEM].m_sFrameTime = 150;
		m_stFrame[i][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
		m_stFrame[i][DEF_OBJECTDAMAGE].m_sFrameTime = 70;
		m_stFrame[i][DEF_OBJECTDYING].m_sMaxFrame = 12;
		m_stFrame[i][DEF_OBJECTDYING].m_sFrameTime = 80;
	}

	int restar = 20;

	m_stFrame[10][DEF_OBJECTSTOP].m_sFrameTime = 240;
	m_stFrame[10][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[10][DEF_OBJECTMOVE].m_sFrameTime = 120 - restar - restar - restar / 1.2;//240;
	m_stFrame[10][DEF_OBJECTATTACK].m_sFrameTime = 90;
	m_stFrame[10][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[10][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[10][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[10][DEF_OBJECTDYING].m_sFrameTime = 240;
	m_stFrame[10][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[11][DEF_OBJECTSTOP].m_sFrameTime = 150;
	m_stFrame[11][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[11][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[11][DEF_OBJECTATTACK].m_sFrameTime = 90;
	m_stFrame[11][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[11][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[11][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[11][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[11][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[12][DEF_OBJECTSTOP].m_sFrameTime = 210;
	m_stFrame[12][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[12][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;//210;
	m_stFrame[12][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[12][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[12][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[12][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[12][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[12][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[13][DEF_OBJECTSTOP].m_sFrameTime = 210;
	m_stFrame[13][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[13][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;//210;
	m_stFrame[13][DEF_OBJECTATTACK].m_sFrameTime = 90;
	m_stFrame[13][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[13][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[13][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[13][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[13][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[14][DEF_OBJECTSTOP].m_sFrameTime = 180;
	m_stFrame[14][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[14][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;//150;
	m_stFrame[14][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[14][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[14][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[14][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[14][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[14][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[15][DEF_OBJECTSTOP].m_sFrameTime = 180;
	m_stFrame[15][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[15][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;//150;
	m_stFrame[15][DEF_OBJECTATTACK].m_sFrameTime = 150;
	m_stFrame[15][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[15][DEF_OBJECTDAMAGE].m_sFrameTime = 180;
	m_stFrame[15][DEF_OBJECTDAMAGE].m_sMaxFrame = 3;
	m_stFrame[15][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[15][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[16][DEF_OBJECTSTOP].m_sFrameTime = 120;
	m_stFrame[16][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[16][DEF_OBJECTMOVE].m_sFrameTime = 60 - restar + 15;//120;
	m_stFrame[16][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[16][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[16][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[16][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[16][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[16][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[17][DEF_OBJECTSTOP].m_sFrameTime = 120;
	m_stFrame[17][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[17][DEF_OBJECTMOVE].m_sFrameTime = 45 - restar + 15;//120;
	m_stFrame[17][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[17][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[17][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[17][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[17][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[17][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[18][DEF_OBJECTSTOP].m_sFrameTime = 210;
	m_stFrame[18][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[18][DEF_OBJECTMOVE].m_sFrameTime = 130 - restar - restar;//270;
	m_stFrame[18][DEF_OBJECTATTACK].m_sFrameTime = 150;
	m_stFrame[18][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[18][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[18][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[18][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[18][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[19][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[19][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[19][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;//210;
	m_stFrame[19][DEF_OBJECTATTACK].m_sFrameTime = 150;
	m_stFrame[19][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[19][DEF_OBJECTDAMAGE].m_sFrameTime = 180;
	m_stFrame[19][DEF_OBJECTDAMAGE].m_sMaxFrame = 3;
	m_stFrame[19][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[19][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[20][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[20][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[20][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;//210;
	m_stFrame[20][DEF_OBJECTATTACK].m_sFrameTime = 150;
	m_stFrame[20][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[20][DEF_OBJECTDAMAGE].m_sFrameTime = 180;
	m_stFrame[20][DEF_OBJECTDAMAGE].m_sMaxFrame = 3;
	m_stFrame[20][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[20][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[21][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[21][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[21][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;//150;
	m_stFrame[21][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[21][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[21][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[21][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[21][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[21][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[22][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[22][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[22][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;//150;
	m_stFrame[22][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[22][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[22][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[22][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[22][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[22][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[23][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[23][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[23][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;//150;
	m_stFrame[23][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[23][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[23][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[23][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[23][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[23][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[24][DEF_OBJECTSTOP].m_sFrameTime = 150;
	m_stFrame[24][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[25][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[25][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[26][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[26][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[27][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[27][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[27][DEF_OBJECTMOVE].m_sFrameTime = 50;
	m_stFrame[27][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[27][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[27][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[27][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[27][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[27][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[28][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[28][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[28][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[28][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[28][DEF_OBJECTATTACK].m_sMaxFrame = 5;
	m_stFrame[28][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[28][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[28][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[28][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[29][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[29][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[29][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[29][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[29][DEF_OBJECTATTACK].m_sMaxFrame = 5;
	m_stFrame[29][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[29][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[29][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[29][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[30][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[30][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[30][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[30][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[30][DEF_OBJECTATTACK].m_sMaxFrame = 5;
	m_stFrame[30][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[30][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[30][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[30][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[31][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[31][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[31][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[31][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[31][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[31][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[31][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[31][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[31][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[32][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[32][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[32][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[32][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[32][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[32][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[32][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[32][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[32][DEF_OBJECTDYING].m_sMaxFrame = 11;
	m_stFrame[33][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[33][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[33][DEF_OBJECTMOVE].m_sFrameTime = 120 - restar - restar;
	m_stFrame[33][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[33][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[33][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[33][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[33][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[33][DEF_OBJECTDYING].m_sMaxFrame = 11;
	m_stFrame[34][DEF_OBJECTSTOP].m_sFrameTime = 240;
	m_stFrame[34][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[34][DEF_OBJECTMOVE].m_sFrameTime = 120 - restar - restar;
	m_stFrame[34][DEF_OBJECTATTACK].m_sFrameTime = 90;
	m_stFrame[34][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[34][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[34][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[34][DEF_OBJECTDYING].m_sFrameTime = 240;
	m_stFrame[34][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[35][DEF_OBJECTSTOP].m_sFrameTime = 80;
	m_stFrame[35][DEF_OBJECTSTOP].m_sMaxFrame = 9;
	m_stFrame[35][DEF_OBJECTMOVE].m_sFrameTime = 20;
	m_stFrame[35][DEF_OBJECTMOVE].m_sMaxFrame = 3;
	m_stFrame[35][DEF_OBJECTATTACK].m_sFrameTime = 80;
	m_stFrame[35][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[35][DEF_OBJECTDAMAGE].m_sFrameTime = 80;
	m_stFrame[35][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[35][DEF_OBJECTDYING].m_sFrameTime = 80;
	m_stFrame[35][DEF_OBJECTDYING].m_sMaxFrame = 7;
	m_stFrame[36][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[36][DEF_OBJECTSTOP].m_sMaxFrame = 0;
	m_stFrame[36][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;
	m_stFrame[36][DEF_OBJECTMOVE].m_sMaxFrame = 0;
	m_stFrame[36][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[36][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[36][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[36][DEF_OBJECTDAMAGE].m_sMaxFrame = 0;
	m_stFrame[36][DEF_OBJECTDYING].m_sFrameTime = 200;
	m_stFrame[36][DEF_OBJECTDYING].m_sMaxFrame = 6;
	m_stFrame[37][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[37][DEF_OBJECTSTOP].m_sMaxFrame = 0;
	m_stFrame[37][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;
	m_stFrame[37][DEF_OBJECTMOVE].m_sMaxFrame = 0;
	m_stFrame[37][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[37][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[37][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[37][DEF_OBJECTDAMAGE].m_sMaxFrame = 0;
	m_stFrame[37][DEF_OBJECTDYING].m_sFrameTime = 200;
	m_stFrame[37][DEF_OBJECTDYING].m_sMaxFrame = 6;
	m_stFrame[38][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[38][DEF_OBJECTSTOP].m_sMaxFrame = 0;
	m_stFrame[38][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;
	m_stFrame[38][DEF_OBJECTMOVE].m_sMaxFrame = 0;
	m_stFrame[38][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[38][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[38][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[38][DEF_OBJECTDAMAGE].m_sMaxFrame = 0;
	m_stFrame[38][DEF_OBJECTDYING].m_sFrameTime = 200;
	m_stFrame[38][DEF_OBJECTDYING].m_sMaxFrame = 6;
	m_stFrame[39][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[39][DEF_OBJECTSTOP].m_sMaxFrame = 0;
	m_stFrame[39][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;
	m_stFrame[39][DEF_OBJECTMOVE].m_sMaxFrame = 0;
	m_stFrame[39][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[39][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[39][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[39][DEF_OBJECTDAMAGE].m_sMaxFrame = 0;
	m_stFrame[39][DEF_OBJECTDYING].m_sFrameTime = 200;
	m_stFrame[39][DEF_OBJECTDYING].m_sMaxFrame = 6;
	m_stFrame[40][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[40][DEF_OBJECTSTOP].m_sMaxFrame = 0;
	m_stFrame[40][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;
	m_stFrame[40][DEF_OBJECTMOVE].m_sMaxFrame = 0;
	m_stFrame[40][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[40][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[40][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[40][DEF_OBJECTDAMAGE].m_sMaxFrame = 0;
	m_stFrame[40][DEF_OBJECTDYING].m_sFrameTime = 200;
	m_stFrame[40][DEF_OBJECTDYING].m_sMaxFrame = 6;
	m_stFrame[41][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[41][DEF_OBJECTSTOP].m_sMaxFrame = 0;
	m_stFrame[41][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;
	m_stFrame[41][DEF_OBJECTMOVE].m_sMaxFrame = 0;
	m_stFrame[41][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[41][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[41][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[41][DEF_OBJECTDAMAGE].m_sMaxFrame = 0;
	m_stFrame[41][DEF_OBJECTDYING].m_sFrameTime = 200;
	m_stFrame[41][DEF_OBJECTDYING].m_sMaxFrame = 6;
	m_stFrame[42][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[42][DEF_OBJECTSTOP].m_sMaxFrame = 0;
	m_stFrame[42][DEF_OBJECTMOVE].m_sFrameTime = 80 - restar;
	m_stFrame[42][DEF_OBJECTMOVE].m_sMaxFrame = 0;
	m_stFrame[42][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[42][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[42][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[42][DEF_OBJECTDAMAGE].m_sMaxFrame = 0;
	m_stFrame[42][DEF_OBJECTDYING].m_sFrameTime = 200;
	m_stFrame[42][DEF_OBJECTDYING].m_sMaxFrame = 0;
	m_stFrame[43][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[43][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[43][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[43][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[43][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[43][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[43][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 7;
	m_stFrame[43][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[43][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[44][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[44][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[44][DEF_OBJECTMOVE].m_sFrameTime = 100 / 1.8;
	m_stFrame[44][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[44][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[44][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[44][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 7;
	m_stFrame[44][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[44][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[45][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[45][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[45][DEF_OBJECTMOVE].m_sFrameTime = 100 / 1.8;
	m_stFrame[45][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[45][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[45][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[45][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 7;
	m_stFrame[45][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[45][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[46][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[46][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[46][DEF_OBJECTMOVE].m_sFrameTime = 100 / 1.8;
	m_stFrame[46][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[46][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[46][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[46][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 7;
	m_stFrame[46][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[46][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[47][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[47][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[47][DEF_OBJECTMOVE].m_sFrameTime = 100 / 1.8;
	m_stFrame[47][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[47][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[47][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[47][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 7;
	m_stFrame[47][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[47][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[48][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[48][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[48][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[48][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[48][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[48][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[48][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 7;
	m_stFrame[48][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[48][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[49][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[49][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[49][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[49][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[49][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[49][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[49][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 7;
	m_stFrame[49][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[49][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[50][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[50][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[50][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[50][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[50][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[50][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[50][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 7;
	m_stFrame[50][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[50][DEF_OBJECTDYING].m_sMaxFrame = 9;
	m_stFrame[51][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[51][DEF_OBJECTSTOP].m_sMaxFrame = 0;
	m_stFrame[51][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[51][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[51][DEF_OBJECTATTACK].m_sMaxFrame = 4;
	m_stFrame[51][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[51][DEF_OBJECTDAMAGE].m_sMaxFrame = 0;
	m_stFrame[51][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[51][DEF_OBJECTDYING].m_sMaxFrame = 6;
	m_stFrame[52][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[52][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[52][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[52][DEF_OBJECTATTACK].m_sFrameTime = 70;
	m_stFrame[52][DEF_OBJECTATTACK].m_sMaxFrame = 9;
	m_stFrame[52][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[52][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[52][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[52][DEF_OBJECTDYING].m_sMaxFrame = 11 + 3;
	m_stFrame[53][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[53][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[53][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[53][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[53][DEF_OBJECTATTACK].m_sMaxFrame = 12;
	m_stFrame[53][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[53][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[53][DEF_OBJECTDYING].m_sFrameTime = 70;
	m_stFrame[53][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[54][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[54][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[54][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[54][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[54][DEF_OBJECTATTACK].m_sMaxFrame = 9;
	m_stFrame[54][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[54][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[54][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[54][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[55][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[55][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[55][DEF_OBJECTMOVE].m_sFrameTime = 70 - restar - restar;
	m_stFrame[55][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[55][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[55][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[55][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[55][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[55][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[56][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[56][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[56][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[56][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[56][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[56][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[56][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[56][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[56][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[57][DEF_OBJECTSTOP].m_sFrameTime = 300;
	m_stFrame[57][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[57][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar - restar;
	m_stFrame[57][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[57][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[57][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[57][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[57][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[57][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[58][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[58][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[58][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[58][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[58][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[58][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[58][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[58][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[58][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[59][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[59][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[59][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[59][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[59][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[59][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[59][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[59][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[59][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[60][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[60][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[60][DEF_OBJECTMOVE].m_sFrameTime = 120 - restar - restar;
	m_stFrame[60][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[60][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[60][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[60][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[60][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[60][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[61][DEF_OBJECTSTOP].m_sFrameTime = 200;
	m_stFrame[61][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[61][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;//60;
	m_stFrame[61][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[61][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[61][DEF_OBJECTDAMAGE].m_sFrameTime = 60;
	m_stFrame[61][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[61][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[61][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[62][DEF_OBJECTSTOP].m_sFrameTime = 200;
	m_stFrame[62][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[62][DEF_OBJECTMOVE].m_sFrameTime = 60 - restar + 15;
	m_stFrame[62][DEF_OBJECTATTACK].m_sFrameTime = 60;
	m_stFrame[62][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[62][DEF_OBJECTDAMAGE].m_sFrameTime = 60;
	m_stFrame[62][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[62][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[62][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[63][DEF_OBJECTSTOP].m_sFrameTime = 200;
	m_stFrame[63][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[63][DEF_OBJECTMOVE].m_sFrameTime = 60 - restar + 15;
	m_stFrame[63][DEF_OBJECTATTACK].m_sFrameTime = 80;
	m_stFrame[63][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[63][DEF_OBJECTDAMAGE].m_sFrameTime = 60;
	m_stFrame[63][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[63][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[63][DEF_OBJECTDYING].m_sMaxFrame = 5 + 3;//7 +3;
	m_stFrame[64][DEF_OBJECTSTOP].m_sFrameTime = 200;
	m_stFrame[64][DEF_OBJECTSTOP].m_sMaxFrame = 40;
	m_stFrame[64][DEF_OBJECTMOVE].m_sFrameTime = 200 - restar - restar;
	m_stFrame[64][DEF_OBJECTATTACK].m_sFrameTime = 200;
	m_stFrame[64][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[64][DEF_OBJECTDAMAGE].m_sFrameTime = 200;
	m_stFrame[64][DEF_OBJECTDAMAGE].m_sMaxFrame = 3;
	m_stFrame[64][DEF_OBJECTDYING].m_sFrameTime = 200;
	m_stFrame[64][DEF_OBJECTDYING].m_sMaxFrame = 3;
	m_stFrame[65][DEF_OBJECTSTOP].m_sFrameTime = 200;
	m_stFrame[65][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[65][DEF_OBJECTMOVE].m_sFrameTime = 140 - restar - restar;
	m_stFrame[65][DEF_OBJECTATTACK].m_sFrameTime = 105;
	m_stFrame[65][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[65][DEF_OBJECTDAMAGE].m_sFrameTime = 60;
	m_stFrame[65][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[65][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[65][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[66][DEF_OBJECTSTOP].m_sFrameTime = 100;
	m_stFrame[66][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[66][DEF_OBJECTMOVE].m_sFrameTime = 90;
	m_stFrame[66][DEF_OBJECTATTACK].m_sFrameTime = 80;
	m_stFrame[66][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[66][DEF_OBJECTDAMAGE].m_sFrameTime = 60;
	m_stFrame[66][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[66][DEF_OBJECTDYING].m_sFrameTime = 65;
	m_stFrame[66][DEF_OBJECTDYING].m_sMaxFrame = 15 + 3;
	m_stFrame[67][DEF_OBJECTSTOP].m_sFrameTime = 200;
	m_stFrame[67][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[67][DEF_OBJECTMOVE].m_sFrameTime = 120 - restar - restar;
	m_stFrame[68][DEF_OBJECTMOVE].m_sMaxFrame = 3;
	m_stFrame[67][DEF_OBJECTATTACK].m_sFrameTime = 80;
	m_stFrame[67][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[67][DEF_OBJECTDAMAGE].m_sFrameTime = 60;
	m_stFrame[67][DEF_OBJECTDAMAGE].m_sMaxFrame = 3;
	m_stFrame[67][DEF_OBJECTDYING].m_sFrameTime = 65;
	m_stFrame[67][DEF_OBJECTDYING].m_sMaxFrame = 3 + 3;
	m_stFrame[68][DEF_OBJECTSTOP].m_sFrameTime = 200;
	m_stFrame[68][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[68][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[68][DEF_OBJECTMOVE].m_sMaxFrame = 3;
	m_stFrame[68][DEF_OBJECTATTACK].m_sFrameTime = 80;
	m_stFrame[68][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[68][DEF_OBJECTDAMAGE].m_sFrameTime = 60;
	m_stFrame[68][DEF_OBJECTDAMAGE].m_sMaxFrame = 3;
	m_stFrame[68][DEF_OBJECTDYING].m_sFrameTime = 65;
	m_stFrame[68][DEF_OBJECTDYING].m_sMaxFrame = 3 + 3;
	m_stFrame[69][DEF_OBJECTSTOP].m_sFrameTime = 200;
	m_stFrame[69][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[69][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[68][DEF_OBJECTMOVE].m_sMaxFrame = 3;
	m_stFrame[69][DEF_OBJECTATTACK].m_sFrameTime = 80;
	m_stFrame[69][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[69][DEF_OBJECTDAMAGE].m_sFrameTime = 60;
	m_stFrame[69][DEF_OBJECTDAMAGE].m_sMaxFrame = 3;
	m_stFrame[69][DEF_OBJECTDYING].m_sFrameTime = 65;
	m_stFrame[69][DEF_OBJECTDYING].m_sMaxFrame = 3 + 3;
	m_stFrame[70][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[70][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[70][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[70][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[70][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[70][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[70][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[70][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[70][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[71][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[71][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[71][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[71][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[71][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[71][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[71][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[71][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[71][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[72][DEF_OBJECTSTOP].m_sFrameTime = 100;
	m_stFrame[72][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[72][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[72][DEF_OBJECTATTACK].m_sFrameTime = 80;
	m_stFrame[72][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[72][DEF_OBJECTDAMAGE].m_sFrameTime = 60;
	m_stFrame[72][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[72][DEF_OBJECTDYING].m_sFrameTime = 65;
	m_stFrame[72][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[73][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[73][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[73][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[73][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[73][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[73][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[73][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[73][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[73][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[74][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[74][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[74][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[74][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[74][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[74][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[74][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[74][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[74][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[75][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[75][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[75][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[75][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[75][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[75][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[75][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[75][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[75][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[76][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[76][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[76][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[76][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[76][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[76][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[76][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[76][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[76][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[77][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[77][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[77][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[77][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[77][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[77][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[77][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[77][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[77][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[78][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[78][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[78][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[78][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[78][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[78][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[78][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[78][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[78][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[79][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[79][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[79][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[79][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[79][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[79][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[79][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[79][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[79][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[80][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[80][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[80][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[80][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[80][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[80][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[80][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[80][DEF_OBJECTDYING].m_sFrameTime = 150;
	m_stFrame[80][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[81][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[81][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[81][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[81][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[81][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[81][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[81][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[81][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[81][DEF_OBJECTDYING].m_sMaxFrame = 15 + 3;
	m_stFrame[82][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[82][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[82][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[82][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[82][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[82][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[82][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[82][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[82][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[83][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[83][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[83][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[83][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[83][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[83][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[83][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[83][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[83][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[84][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[84][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[84][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[84][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[84][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[84][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[84][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[84][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[84][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[85][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[85][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[85][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[85][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[85][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[85][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[85][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[85][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[85][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[86][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[86][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[86][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[86][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[86][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[86][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[86][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[86][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[86][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[87][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[87][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[87][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[87][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[87][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[87][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[87][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[87][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[87][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[88][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[88][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[88][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[88][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[88][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[88][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[88][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[88][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[88][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[89][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[89][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[89][DEF_OBJECTMOVE].m_sFrameTime = 90 - restar;
	m_stFrame[89][DEF_OBJECTATTACK].m_sFrameTime = 100;
	m_stFrame[89][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[89][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[89][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[89][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[89][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;
	m_stFrame[90][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[90][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[91][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[91][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[91][DEF_OBJECTDAMAGE].m_sFrameTime = 100;
	m_stFrame[91][DEF_OBJECTDAMAGE].m_sMaxFrame = 7;
	m_stFrame[91][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[91][DEF_OBJECTDYING].m_sMaxFrame = 7 + 3;

	m_stFrame[99][DEF_OBJECTSTOP].m_sFrameTime = 250;
	m_stFrame[99][DEF_OBJECTSTOP].m_sMaxFrame = 3;
	m_stFrame[99][DEF_OBJECTMOVE].m_sFrameTime = 100 - restar;
	m_stFrame[99][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[99][DEF_OBJECTATTACK].m_sMaxFrame = 7;
	m_stFrame[99][DEF_OBJECTDAMAGE].m_sFrameTime = 120;
	m_stFrame[99][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[99][DEF_OBJECTDYING].m_sFrameTime = 100;
	m_stFrame[99][DEF_OBJECTDYING].m_sMaxFrame = 9;

	m_stFrame[110][DEF_OBJECTSTOP].m_sFrameTime = 40;
	m_stFrame[110][DEF_OBJECTSTOP].m_sMaxFrame = 7;
	m_stFrame[110][DEF_OBJECTMOVE].m_sFrameTime = 80;//150;
	m_stFrame[110][DEF_OBJECTATTACK].m_sFrameTime = 120;
	m_stFrame[110][DEF_OBJECTATTACK].m_sMaxFrame = 3;
	m_stFrame[110][DEF_OBJECTDAMAGE].m_sFrameTime = 150;
	m_stFrame[110][DEF_OBJECTDAMAGE].m_sMaxFrame = 3 + 4;
	m_stFrame[110][DEF_OBJECTDYING].m_sFrameTime = 180;
	m_stFrame[110][DEF_OBJECTDYING].m_sMaxFrame = 7;

}

void CMapData::Init()
{
	int x, y;
	m_dwFrameCheckTime = GameClock::GetTimeMS();
	m_dwFrameAdjustTime = 0;
	m_sPivotX = -1;
	m_sPivotY = -1;

	for (x = 0; x < MAPDATASIZEX; x++)
		for (y = 0; y < MAPDATASIZEY; y++)
			m_pData[x][y].Clear();

	for (x = 0; x < 30000; x++) {
		m_iObjectIDcacheLocX[x] = 0;
		m_iObjectIDcacheLocY[x] = 0;
	}
}

CMapData::~CMapData()
{
}

void CMapData::OpenMapDataFile(char* cFn)
{
	HANDLE hFileRead;
	DWORD nCount;
	char cHeader[260];
	char* cp, * cpMapData;
	std::memset(cHeader, 0, sizeof(cHeader));
	hFileRead = CreateFile(cFn, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
	if (hFileRead == INVALID_HANDLE_VALUE) return;
	SetFilePointer(hFileRead, 0, 0, FILE_BEGIN);
	ReadFile(hFileRead, cHeader, 256, &nCount, 0);
	_bDecodeMapInfo(cHeader);
	cpMapData = new char[m_sMapSizeX * m_sMapSizeY * 10];
	ReadFile(hFileRead, cpMapData, m_sMapSizeX * m_sMapSizeY * 10, &nCount, 0);
	CloseHandle(hFileRead);
	cp = cpMapData;
	short* sp;
	for (int y = 0; y < m_sMapSizeY; y++)
	{
		for (int x = 0; x < m_sMapSizeX; x++)
		{
			sp = (short*)cp;
			m_tile[x][y].m_sTileSprite = *sp;
			cp += 2;
			sp = (short*)cp;
			m_tile[x][y].m_sTileSpriteFrame = *sp;
			cp += 2;
			sp = (short*)cp;
			m_tile[x][y].m_sObjectSprite = *sp;
			cp += 2;
			sp = (short*)cp;
			m_tile[x][y].m_sObjectSpriteFrame = *sp;
			cp += 2;
			if (((*cp) & 0x80) != 0)
				m_tile[x][y].m_bIsMoveAllowed = false;
			else m_tile[x][y].m_bIsMoveAllowed = true;
			if (((*cp) & 0x40) != 0)
				m_tile[x][y].m_bIsTeleport = true;
			else m_tile[x][y].m_bIsTeleport = false;
			cp += 2;
		}
	}
	delete[] cpMapData;
}

void CMapData::_bDecodeMapInfo(char* pHeader)
{
	int i;
	char* token, cReadMode;
	char seps[] = "= ,\t\n";
	for (i = 0; i < 256; i++)
		if (pHeader[i] == 0) pHeader[i] = ' ';

	cReadMode = 0;

	token = strtok(pHeader, seps);
	while (token != 0)
	{
		if (cReadMode != 0)
		{
			switch (cReadMode)
			{
			case 1:
				m_sMapSizeX = atoi(token);
				cReadMode = 0;
				break;
			case 2:
				m_sMapSizeY = atoi(token);
				cReadMode = 0;
				break;
			}
		}
		else
		{
			if (memcmp(token, "MAPSIZEX", 8) == 0) cReadMode = 1;
			if (memcmp(token, "MAPSIZEY", 8) == 0) cReadMode = 2;
		}
		token = strtok(0, seps);
	}
}

void CMapData::ShiftMapData(char cDir)
{
	int ix, iy;
	for (iy = 0; iy < MAPDATASIZEY; iy++)
		for (ix = 0; ix < MAPDATASIZEX; ix++)
			m_pTmpData[ix][iy].Clear();

	switch (cDir) {
	case 1: // North
		for (ix = 0; ix < DEF_INITDATA_TILES_X + 1; ix++)
			for (iy = 0; iy < DEF_INITDATA_TILES_Y; iy++)
				memcpy(&m_pTmpData[DEF_MAPDATA_BUFFER_X + ix][DEF_MAPDATA_BUFFER_Y + 1 + iy], &m_pData[DEF_MAPDATA_BUFFER_X + ix][DEF_MAPDATA_BUFFER_Y + iy], sizeof(class CTile));
		m_sPivotY--;
		break;
	case 2: // NE
		for (ix = 0; ix < DEF_INITDATA_TILES_X; ix++)
			for (iy = 0; iy < DEF_INITDATA_TILES_Y; iy++)
				memcpy(&m_pTmpData[DEF_MAPDATA_BUFFER_X + ix][DEF_MAPDATA_BUFFER_Y + 1 + iy], &m_pData[DEF_MAPDATA_BUFFER_Y + ix][DEF_MAPDATA_BUFFER_Y + iy], sizeof(class CTile));
		m_sPivotX++;
		m_sPivotY--;
		break;
	case 3: // East
		for (ix = 0; ix < DEF_INITDATA_TILES_X; ix++)
			for (iy = 0; iy < DEF_INITDATA_TILES_Y + 1; iy++)
				memcpy(&m_pTmpData[DEF_MAPDATA_BUFFER_X + ix][DEF_MAPDATA_BUFFER_Y + iy], &m_pData[DEF_MAPDATA_BUFFER_Y + ix][DEF_MAPDATA_BUFFER_Y + iy], sizeof(class CTile));
		m_sPivotX++;
		break;
	case 4: // SE
		for (ix = 0; ix < DEF_INITDATA_TILES_X; ix++)
			for (iy = 0; iy < DEF_INITDATA_TILES_Y; iy++)
				memcpy(&m_pTmpData[DEF_MAPDATA_BUFFER_X + ix][DEF_MAPDATA_BUFFER_Y + iy], &m_pData[DEF_MAPDATA_BUFFER_Y + ix][DEF_MAPDATA_BUFFER_Y + 1 + iy], sizeof(class CTile));
		m_sPivotX++;
		m_sPivotY++;
		break;
	case 5: // South
		for (ix = 0; ix < DEF_INITDATA_TILES_X + 1; ix++)
			for (iy = 0; iy < DEF_INITDATA_TILES_Y; iy++)
				memcpy(&m_pTmpData[DEF_MAPDATA_BUFFER_X + ix][DEF_MAPDATA_BUFFER_Y + iy], &m_pData[DEF_MAPDATA_BUFFER_X + ix][DEF_MAPDATA_BUFFER_Y + 1 + iy], sizeof(class CTile));
		m_sPivotY++;
		break;
	case 6: // SW
		for (ix = 0; ix < DEF_INITDATA_TILES_X; ix++)
			for (iy = 0; iy < DEF_INITDATA_TILES_Y; iy++)
				memcpy(&m_pTmpData[DEF_MAPDATA_BUFFER_Y + ix][DEF_MAPDATA_BUFFER_Y + iy], &m_pData[DEF_MAPDATA_BUFFER_X + ix][DEF_MAPDATA_BUFFER_Y + 1 + iy], sizeof(class CTile));
		m_sPivotX--;
		m_sPivotY++;
		break;
	case 7: // West
		for (ix = 0; ix < DEF_INITDATA_TILES_X; ix++)
			for (iy = 0; iy < DEF_INITDATA_TILES_Y + 1; iy++)
				memcpy(&m_pTmpData[DEF_MAPDATA_BUFFER_Y + ix][DEF_MAPDATA_BUFFER_Y + iy], &m_pData[DEF_MAPDATA_BUFFER_X + ix][DEF_MAPDATA_BUFFER_Y + iy], sizeof(class CTile));
		m_sPivotX--;
		break;
	case 8: // NW
		for (ix = 0; ix < DEF_INITDATA_TILES_X; ix++)
			for (iy = 0; iy < DEF_INITDATA_TILES_Y; iy++)
				memcpy(&m_pTmpData[DEF_MAPDATA_BUFFER_Y + ix][DEF_MAPDATA_BUFFER_Y + 1 + iy], &m_pData[DEF_MAPDATA_BUFFER_X + ix][DEF_MAPDATA_BUFFER_Y + iy], sizeof(class CTile));
		m_sPivotX--;
		m_sPivotY--;
		break;
	}
	memcpy(&m_pData[0][0], &m_pTmpData[0][0], sizeof(m_pData));
}

bool CMapData::bGetIsLocateable(short sX, short sY)
{
	int dX, dY;
	if ((sX < m_sPivotX) || (sX > m_sPivotX + MAPDATASIZEX) ||
		(sY < m_sPivotY) || (sY > m_sPivotY + MAPDATASIZEY)) return false;
	dX = sX - m_sPivotX;
	dY = sY - m_sPivotY;
	//Helltrayn 28/05/09. A�adimos esto para corregir el bug MIM que cierra el cliente
	if (dX <= 0 || dY <= 0) return false;
	if (m_pData[dX][dY].m_sOwnerType != 0) return false;
	if (m_tile[sX][sY].m_bIsMoveAllowed == false) return false;
	if (m_pData[dX][dY].m_sDynamicObjectType == DEF_DYNAMICOBJECT_MINERAL1) return false; // 4
	if (m_pData[dX][dY].m_sDynamicObjectType == DEF_DYNAMICOBJECT_MINERAL2) return false; // 5

	if (m_pData[dX + 1][dY + 1].m_sOwnerType == hb::owner::Wyvern) return false;
	if (m_pData[dX + 1][dY].m_sOwnerType == hb::owner::Wyvern) return false;
	if ((dY > 0) && (m_pData[dX + 1][dY - 1].m_sOwnerType == hb::owner::Wyvern)) return false;
	if (m_pData[dX][dY + 1].m_sOwnerType == hb::owner::Wyvern) return false;
	if (m_pData[dX][dY].m_sOwnerType == hb::owner::Wyvern) return false;
	if ((dY > 0) && (m_pData[dX][dY - 1].m_sOwnerType == hb::owner::Wyvern)) return false;
	if ((dX > 0) && (m_pData[dX - 1][dY + 1].m_sOwnerType == hb::owner::Wyvern)) return false;
	if ((dX > 0) && (m_pData[dX - 1][dY].m_sOwnerType == hb::owner::Wyvern)) return false;
	if ((dX > 0) && (dY > 0) && (m_pData[dX - 1][dY - 1].m_sOwnerType == hb::owner::Wyvern)) return false;
	if (m_pData[dX + 1][dY + 1].m_sOwnerType == hb::owner::FireWyvern) return false;
	if (m_pData[dX + 1][dY].m_sOwnerType == hb::owner::FireWyvern) return false;
	if ((dY > 0) && (m_pData[dX + 1][dY - 1].m_sOwnerType == hb::owner::FireWyvern)) return false;
	if (m_pData[dX][dY + 1].m_sOwnerType == hb::owner::FireWyvern) return false;
	if (m_pData[dX][dY].m_sOwnerType == hb::owner::FireWyvern) return false;
	if ((dY > 0) && (m_pData[dX][dY - 1].m_sOwnerType == hb::owner::FireWyvern)) return false;
	if ((dX > 0) && (m_pData[dX - 1][dY + 1].m_sOwnerType == hb::owner::FireWyvern)) return false;
	if ((dX > 0) && (m_pData[dX - 1][dY].m_sOwnerType == hb::owner::FireWyvern)) return false;
	if ((dX > 0) && (dY > 0) && (m_pData[dX - 1][dY - 1].m_sOwnerType == hb::owner::FireWyvern)) return false;
	if (m_pData[dX + 1][dY + 1].m_sOwnerType == hb::owner::Abaddon) return false;
	if (m_pData[dX + 1][dY].m_sOwnerType == hb::owner::Abaddon) return false;
	if ((dY > 0) && (m_pData[dX + 1][dY - 1].m_sOwnerType == hb::owner::Abaddon)) return false;
	if (m_pData[dX][dY + 1].m_sOwnerType == hb::owner::Abaddon) return false;
	if (m_pData[dX][dY].m_sOwnerType == hb::owner::Abaddon) return false;
	if ((dY > 0) && (m_pData[dX][dY - 1].m_sOwnerType == hb::owner::Abaddon)) return false;
	if ((dX > 0) && (m_pData[dX - 1][dY + 1].m_sOwnerType == hb::owner::Abaddon)) return false;
	if ((dX > 0) && (m_pData[dX - 1][dY].m_sOwnerType == hb::owner::Abaddon)) return false;
	if ((dX > 0) && (dY > 0) && (m_pData[dX - 1][dY - 1].m_sOwnerType == hb::owner::Abaddon)) return false;
	if (m_pData[dX + 1][dY + 1].m_sOwnerType == hb::owner::Gate) return false;
	if (m_pData[dX + 1][dY].m_sOwnerType == hb::owner::Gate) return false;
	if ((dY > 0) && (m_pData[dX + 1][dY - 1].m_sOwnerType == hb::owner::Gate)) return false;
	if (m_pData[dX][dY + 1].m_sOwnerType == hb::owner::Gate) return false;
	if (m_pData[dX][dY].m_sOwnerType == hb::owner::Gate) return false;
	if ((dY > 0) && (m_pData[dX][dY - 1].m_sOwnerType == hb::owner::Gate)) return false;
	if ((dX > 0) && (m_pData[dX - 1][dY + 1].m_sOwnerType == hb::owner::Gate)) return false;
	if ((dX > 0) && (m_pData[dX - 1][dY].m_sOwnerType == hb::owner::Gate)) return false;
	if ((dX > 0) && (dY > 0) && (m_pData[dX - 1][dY - 1].m_sOwnerType == hb::owner::Gate)) return false;
	return true;
}

bool CMapData::bIsTeleportLoc(short sX, short sY)
{
	if ((sX < m_sPivotX) || (sX > m_sPivotX + MAPDATASIZEX) ||
		(sY < m_sPivotY) || (sY > m_sPivotY + MAPDATASIZEY)) return false;

	if (m_tile[sX][sY].m_bIsTeleport == false) return false;

	return true;
}

bool __fastcall CMapData::bSetOwner(uint16_t wObjectID, int sX, int sY, int sType, int cDir, const PlayerAppearance& appearance, const PlayerStatus& status, char* pName, short sAction, short sV1, short sV2, short sV3, int iPreLoc, int iFrame)
{
	int   iX, iY, dX, dY;
	int   iChatIndex, iAdd;
	char  cTmpName[12];
	uint32_t dwTime;
	int   iEffectType, iEffectFrame, iEffectTotalFrame;
	bool  bUseAbsPos = false;
	uint16_t wOriginalObjectID = wObjectID;
	PlayerStatus localStatus = status;
	PlayerAppearance localAppearance = appearance;
	// Track old motion offset for seamless tile transitions during continuous movement
	float fOldMotionOffsetX = 0.0f;
	float fOldMotionOffsetY = 0.0f;
	int8_t cOldMotionDir = 0;
	bool bHadOldMotion = false;

	if ((m_sPivotX == -1) || (m_sPivotY == -1)) return false;
	std::memset(cTmpName, 0, sizeof(cTmpName));
	strcpy(cTmpName, pName);
	dwTime = m_dwFrameTime;
	iEffectType = iEffectFrame = iEffectTotalFrame = 0;
	if ((hb::objectid::IsNearbyOffset(wObjectID)) &&
		((sAction == DEF_OBJECTMOVE) || (sAction == DEF_OBJECTRUN) ||
			(sAction == DEF_OBJECTDAMAGEMOVE) || (sAction == DEF_OBJECTDAMAGE) ||
			(sAction == DEF_OBJECTDYING))) {
		if ((sX >= m_sPivotX) && (sX < m_sPivotX + MAPDATASIZEX) &&
			(sY >= m_sPivotY) && (sY < m_sPivotY + MAPDATASIZEY)) {
			bUseAbsPos = true;
		}
	}
	if ((!hb::objectid::IsNearbyOffset(wObjectID))
		&& ((sX < m_sPivotX) || (sX >= m_sPivotX + MAPDATASIZEX)
			|| (sY < m_sPivotY) || (sY >= m_sPivotY + MAPDATASIZEY)))
	{
		if (m_iObjectIDcacheLocX[wObjectID] > 0)
		{
			iX = m_iObjectIDcacheLocX[wObjectID] - m_sPivotX;
			iY = m_iObjectIDcacheLocY[wObjectID] - m_sPivotY;
			if ((iX < 0) || (iX >= MAPDATASIZEX) || (iY < 0) || (iY >= MAPDATASIZEY))
			{
				m_iObjectIDcacheLocX[wObjectID] = 0;
				m_iObjectIDcacheLocY[wObjectID] = 0;
				return false;
			}

			if (m_pData[iX][iY].m_wObjectID == wObjectID)
			{
				m_pData[iX][iY].m_sOwnerType = 0;
				std::memset(m_pData[iX][iY].m_cOwnerName, 0, sizeof(m_pData[iX][iY].m_cOwnerName));
				std::memset(pName, 0, strlen(pName));

				if (m_pGame->m_pChatMsgList[m_pData[iX][iY].m_iChatMsg])
				{
					m_pGame->m_pChatMsgList[m_pData[iX][iY].m_iChatMsg].reset();
				}
				m_pData[iX][iY].m_iChatMsg = 0;
				m_pData[iX][iY].m_iEffectType = 0;
				m_iObjectIDcacheLocX[wObjectID] = 0;
				m_iObjectIDcacheLocY[wObjectID] = 0;
				return false;
			}
		}
		else if (m_iObjectIDcacheLocX[wObjectID] < 0)
		{
			iX = abs(m_iObjectIDcacheLocX[wObjectID]) - m_sPivotX;
			iY = abs(m_iObjectIDcacheLocY[wObjectID]) - m_sPivotY;
			if ((iX < 0) || (iX >= MAPDATASIZEX) || (iY < 0) || (iY >= MAPDATASIZEY))
			{
				m_iObjectIDcacheLocX[wObjectID] = 0;
				m_iObjectIDcacheLocY[wObjectID] = 0;
				return false;
			}
			if ((m_pData[iX][iY].m_cDeadOwnerFrame == -1) && (m_pData[iX][iY].m_wDeadObjectID == wObjectID))
			{
				m_pData[iX][iY].m_cDeadOwnerFrame = 0;
				std::memset(pName, 0, strlen(pName));
				if (m_pGame->m_pChatMsgList[m_pData[iX][iY].m_iDeadChatMsg])
				{
					m_pGame->m_pChatMsgList[m_pData[iX][iY].m_iDeadChatMsg].reset();
				}
				m_pData[iX][iY].m_iDeadChatMsg = 0;
				m_iObjectIDcacheLocX[wObjectID] = 0;
				m_iObjectIDcacheLocY[wObjectID] = 0;
				return false;
			}
		}

		for (iX = 0; iX < MAPDATASIZEX; iX++)
			for (iY = 0; iY < MAPDATASIZEY; iY++)
			{
				if (m_pData[iX][iY].m_wObjectID == wObjectID)
				{
					m_pData[iX][iY].m_sOwnerType = 0;
					std::memset(m_pData[iX][iY].m_cOwnerName, 0, sizeof(m_pData[iX][iY].m_cOwnerName));
					std::memset(pName, 0, strlen(pName));
					if (m_pGame->m_pChatMsgList[m_pData[iX][iY].m_iChatMsg])
					{
						m_pGame->m_pChatMsgList[m_pData[iX][iY].m_iChatMsg].reset();
					}
					m_pData[iX][iY].m_iChatMsg = 0;
					m_iObjectIDcacheLocX[wObjectID] = 0;
					m_iObjectIDcacheLocY[wObjectID] = 0;
					m_pData[iX][iY].m_iEffectType = 0;
					return false;
				}

				if ((m_pData[iX][iY].m_cDeadOwnerFrame == -1) && (m_pData[iX][iY].m_wDeadObjectID == wObjectID))
				{
					m_pData[iX][iY].m_cDeadOwnerFrame = 0;
					std::memset(pName, 0, strlen(pName));
					if (m_pGame->m_pChatMsgList[m_pData[iX][iY].m_iDeadChatMsg])
					{
						m_pGame->m_pChatMsgList[m_pData[iX][iY].m_iDeadChatMsg].reset();
					}
					m_pData[iX][iY].m_iDeadChatMsg = 0;
					m_iObjectIDcacheLocX[wObjectID] = 0;
					m_iObjectIDcacheLocY[wObjectID] = 0;
					return false;
				}
			}
		std::memset(pName, 0, strlen(pName));
		return false;
	}
	iChatIndex = 0;

	if ((!hb::objectid::IsNearbyOffset(wObjectID)) && (sAction != DEF_OBJECTNULLACTION))
	{
		std::memset(cTmpName, 0, sizeof(cTmpName));
		strcpy(cTmpName, pName);
		dX = sX - m_sPivotX;
		dY = sY - m_sPivotY;
		if (m_iObjectIDcacheLocX[wObjectID] > 0)
		{
			iX = m_iObjectIDcacheLocX[wObjectID] - m_sPivotX;
			iY = m_iObjectIDcacheLocY[wObjectID] - m_sPivotY;
			if ((iX < 0) || (iX >= MAPDATASIZEX) || (iY < 0) || (iY >= MAPDATASIZEY))
			{
				m_iObjectIDcacheLocX[wObjectID] = 0;
				m_iObjectIDcacheLocY[wObjectID] = 0;
				return false;
			}
			if (m_pData[iX][iY].m_wObjectID == wObjectID)
			{
				iChatIndex = m_pData[iX][iY].m_iChatMsg;
				iEffectType = m_pData[iX][iY].m_iEffectType;
				iEffectFrame = m_pData[iX][iY].m_iEffectFrame;
				iEffectTotalFrame = m_pData[iX][iY].m_iEffectTotalFrame;

				// Capture old motion offset and direction for seamless continuous movement
				if (m_pData[iX][iY].m_motion.bIsMoving) {
					fOldMotionOffsetX = m_pData[iX][iY].m_motion.fCurrentOffsetX;
					fOldMotionOffsetY = m_pData[iX][iY].m_motion.fCurrentOffsetY;
					cOldMotionDir = m_pData[iX][iY].m_motion.cDirection;
					bHadOldMotion = true;
				}

				m_pData[iX][iY].m_wObjectID = 0; //-1; v1.41
				m_pData[iX][iY].m_iChatMsg = 0; // v1.4
				m_pData[iX][iY].m_sOwnerType = 0;
				std::memset(m_pData[iX][iY].m_cOwnerName, 0, sizeof(m_pData[iX][iY].m_cOwnerName));
				m_iObjectIDcacheLocX[wObjectID] = sX;
				m_iObjectIDcacheLocY[wObjectID] = sY;
				goto EXIT_SEARCH_LOOP;
			}
		}
		else if (m_iObjectIDcacheLocX[wObjectID] < 0)
		{
			iX = abs(m_iObjectIDcacheLocX[wObjectID]) - m_sPivotX;
			iY = abs(m_iObjectIDcacheLocY[wObjectID]) - m_sPivotY;
			if ((iX < 0) || (iX >= MAPDATASIZEX) || (iY < 0) || (iY >= MAPDATASIZEY))
			{
				m_iObjectIDcacheLocX[wObjectID] = 0;
				m_iObjectIDcacheLocY[wObjectID] = 0;
				return false;
			}
			if ((m_pData[iX][iY].m_cDeadOwnerFrame == -1) && (m_pData[iX][iY].m_wDeadObjectID == wObjectID))
			{
				iChatIndex = m_pData[iX][iY].m_iDeadChatMsg;
				iEffectType = m_pData[iX][iY].m_iEffectType;
				iEffectFrame = m_pData[iX][iY].m_iEffectFrame;
				iEffectTotalFrame = m_pData[iX][iY].m_iEffectTotalFrame;
				m_pData[iX][iY].m_wDeadObjectID = 0;
				m_pData[iX][iY].m_iDeadChatMsg = 0; // v1.4
				m_pData[iX][iY].m_sDeadOwnerType = 0;
				m_iObjectIDcacheLocX[wObjectID] = -1 * sX;
				m_iObjectIDcacheLocY[wObjectID] = -1 * sY;
				goto EXIT_SEARCH_LOOP;
			}
		}

		iAdd = 7;
		for (iX = sX - iAdd; iX <= sX + iAdd; iX++)
			for (iY = sY - iAdd; iY <= sY + iAdd; iY++)
			{
				if (iX < m_sPivotX) break;
				else if (iX >= m_sPivotX + MAPDATASIZEX) break;
				if (iY < m_sPivotY) break;
				else if (iY >= m_sPivotY + MAPDATASIZEY) break;
				//if (memcmp(m_pData[iX - m_sPivotX][iY - m_sPivotY].m_cOwnerName, cTmpName, 10) == 0) {
				if (m_pData[iX - m_sPivotX][iY - m_sPivotY].m_wObjectID == wObjectID)
				{
					iChatIndex = m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iChatMsg;
					iEffectType = m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iEffectType;
					iEffectFrame = m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iEffectFrame;
					iEffectTotalFrame = m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iEffectTotalFrame;
					m_pData[iX - m_sPivotX][iY - m_sPivotY].m_wObjectID = 0; //-1; v1.41
					m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iChatMsg = 0;
					m_pData[iX - m_sPivotX][iY - m_sPivotY].m_sOwnerType = 0;
					m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iEffectType = 0;
					std::memset(m_pData[iX - m_sPivotX][iY - m_sPivotY].m_cOwnerName, 0, sizeof(m_pData[iX - m_sPivotX][iY - m_sPivotY].m_cOwnerName));
					m_iObjectIDcacheLocX[wObjectID] = sX;
					m_iObjectIDcacheLocY[wObjectID] = sY;
					goto EXIT_SEARCH_LOOP;
				}

				//if (memcmp(m_pData[iX - m_sPivotX][iY - m_sPivotY].m_cDeadOwnerName, cTmpName, 10) == 0) {
				if (m_pData[iX - m_sPivotX][iY - m_sPivotY].m_wDeadObjectID == wObjectID)
				{
					iChatIndex = m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iDeadChatMsg;
					iEffectType = m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iEffectType;
					iEffectFrame = m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iEffectFrame;
					iEffectTotalFrame = m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iEffectTotalFrame;
					m_pData[iX - m_sPivotX][iY - m_sPivotY].m_wDeadObjectID = 0; //-1; v1.41
					m_pData[iX - m_sPivotX][iY - m_sPivotY].m_iDeadChatMsg = 0;
					m_pData[iX - m_sPivotX][iY - m_sPivotY].m_sDeadOwnerType = 0;
					std::memset(m_pData[iX - m_sPivotX][iY - m_sPivotY].m_cDeadOwnerName, 0, sizeof(m_pData[iX - m_sPivotX][iY - m_sPivotY].m_cDeadOwnerName));
					m_iObjectIDcacheLocX[wObjectID] = -1 * sX;
					m_iObjectIDcacheLocY[wObjectID] = -1 * sY;
					goto EXIT_SEARCH_LOOP;
				}
			}
		m_iObjectIDcacheLocX[wObjectID] = sX;
		m_iObjectIDcacheLocY[wObjectID] = sY;
	}
	else
	{
		if (sAction != DEF_OBJECTNULLACTION)// ObjectID
			wObjectID -= 30000;
		// v1.5 Crash
		if (hb::objectid::IsNearbyOffset(wObjectID)) return false;
		if (m_iObjectIDcacheLocX[wObjectID] > 0)
		{
			iX = m_iObjectIDcacheLocX[wObjectID] - m_sPivotX;
			iY = m_iObjectIDcacheLocY[wObjectID] - m_sPivotY;
			if ((iX < 0) || (iX >= MAPDATASIZEX) || (iY < 0) || (iY >= MAPDATASIZEY))
			{
				m_iObjectIDcacheLocX[wObjectID] = 0;
				m_iObjectIDcacheLocY[wObjectID] = 0;
				return false;
			}
			if (m_pData[iX][iY].m_wObjectID == wObjectID)
			{
				dX = iX;
				dY = iY;
				if (bUseAbsPos) {
					dX = sX - m_sPivotX;
					dY = sY - m_sPivotY;
				}
				else {
					switch (sAction) {
					case DEF_OBJECTRUN:
					case DEF_OBJECTMOVE:
					case DEF_OBJECTDAMAGEMOVE:
					case DEF_OBJECTATTACKMOVE:
						switch (cDir) {
						case 1: dY--; break;
						case 2: dY--; dX++; break;
						case 3: dX++; break;
						case 4: dX++; dY++; break;
						case 5: dY++; break;
						case 6: dX--; dY++; break;
						case 7: dX--; break;
						case 8: dX--; dY--; break;
						}
						break;
					default:
						break;
					}
				}
				if ((wObjectID != (WORD)m_pGame->m_pPlayer->m_sPlayerObjectID)
					&& (m_pData[dX][dY].m_sOwnerType != 0) && (m_pData[dX][dY].m_wObjectID != wObjectID))
				{
					m_pGame->RequestFullObjectData(wObjectID);
					std::memset(pName, 0, strlen(pName));
					return false;
				}
				iChatIndex = m_pData[iX][iY].m_iChatMsg;
				if (sAction != DEF_OBJECTNULLACTION)
				{
					sType = m_pData[iX][iY].m_sOwnerType;
					localAppearance = m_pData[iX][iY].m_appearance;
					localStatus = m_pData[iX][iY].m_status;
					iEffectType = m_pData[iX][iY].m_iEffectType;
					iEffectFrame = m_pData[iX][iY].m_iEffectFrame;
					iEffectTotalFrame = m_pData[iX][iY].m_iEffectTotalFrame;
				}
				std::memset(cTmpName, 0, sizeof(cTmpName));
				memcpy(cTmpName, m_pData[iX][iY].m_cOwnerName, 10);
				std::memset(pName, 0, sizeof(pName));
				memcpy(pName, m_pData[iX][iY].m_cOwnerName, 10);
				m_pData[iX][iY].m_wObjectID = 0; //-1; v1.41
				m_pData[iX][iY].m_iChatMsg = 0;
				m_pData[iX][iY].m_sOwnerType = 0;
				m_pData[iX][iY].m_iEffectType = 0;
				std::memset(m_pData[iX][iY].m_cOwnerName, 0, sizeof(m_pData[iX][iY].m_cOwnerName));
				m_iObjectIDcacheLocX[wObjectID] = dX + m_sPivotX;
				m_iObjectIDcacheLocY[wObjectID] = dY + m_sPivotY;
				goto EXIT_SEARCH_LOOP;
			}
		}
		else if (m_iObjectIDcacheLocX[wObjectID] < 0)
		{
			iX = abs(m_iObjectIDcacheLocX[wObjectID]) - m_sPivotX;
			iY = abs(m_iObjectIDcacheLocY[wObjectID]) - m_sPivotY;
			if ((iX < 0) || (iX >= MAPDATASIZEX) || (iY < 0) || (iY >= MAPDATASIZEY))
			{
				m_iObjectIDcacheLocX[wObjectID] = 0;
				m_iObjectIDcacheLocY[wObjectID] = 0;
				return false;
			}
			if ((m_pData[iX][iY].m_cDeadOwnerFrame == -1) && (m_pData[iX][iY].m_wDeadObjectID == wObjectID))
			{
				dX = iX;
				dY = iY;
				if (bUseAbsPos) {
					dX = sX - m_sPivotX;
					dY = sY - m_sPivotY;
				}
				else {
					switch (sAction) {
					case DEF_OBJECTMOVE:
					case DEF_OBJECTRUN:
					case DEF_OBJECTDAMAGEMOVE:
					case DEF_OBJECTATTACKMOVE:
						switch (cDir) {
						case 1: dY--; break;
						case 2: dY--; dX++; break;
						case 3: dX++; break;
						case 4: dX++; dY++; break;
						case 5: dY++; break;
						case 6: dX--; dY++; break;
						case 7: dX--; break;
						case 8: dX--; dY--; break;
						}
						break;
					default:
						break;
					}
				}
				if ((wObjectID != (WORD)m_pGame->m_pPlayer->m_sPlayerObjectID) &&
					(m_pData[dX][dY].m_sOwnerType != 0) && (m_pData[dX][dY].m_wObjectID != wObjectID))
				{
					m_pGame->RequestFullObjectData(wObjectID);
					std::memset(pName, 0, strlen(pName));
					return false;
				}
				iChatIndex = m_pData[iX][iY].m_iDeadChatMsg;
				if (sAction != DEF_OBJECTNULLACTION) {
					sType = m_pData[iX][iY].m_sDeadOwnerType;
					localAppearance = m_pData[iX][iY].m_deadAppearance;
					localStatus = m_pData[iX][iY].m_deadStatus;
				}
				std::memset(cTmpName, 0, sizeof(cTmpName));
				memcpy(cTmpName, m_pData[iX][iY].m_cDeadOwnerName, 10);
				std::memset(pName, 0, sizeof(pName));
				memcpy(pName, m_pData[iX][iY].m_cDeadOwnerName, 10);
				m_pData[iX][iY].m_wDeadObjectID = 0; // -1; v1.41
				m_pData[iX][iY].m_iDeadChatMsg = 0;
				m_pData[iX][iY].m_sDeadOwnerType = 0;
				std::memset(m_pData[iX][iY].m_cDeadOwnerName, 0, sizeof(m_pData[iX][iY].m_cDeadOwnerName));
				m_iObjectIDcacheLocX[wObjectID] = -1 * (dX + m_sPivotX);
				m_iObjectIDcacheLocY[wObjectID] = -1 * (dY + m_sPivotY);
				goto EXIT_SEARCH_LOOP;
			}
		}

		for (iX = 0; iX < MAPDATASIZEX; iX++)
			for (iY = 0; iY < MAPDATASIZEY; iY++)
			{
				if (m_pData[iX][iY].m_wObjectID == wObjectID)
				{
					dX = iX;
					dY = iY;
					if (bUseAbsPos) {
						dX = sX - m_sPivotX;
						dY = sY - m_sPivotY;
					}
					else {
						switch (sAction) {
						case DEF_OBJECTRUN:
						case DEF_OBJECTMOVE:
						case DEF_OBJECTDAMAGEMOVE:
						case DEF_OBJECTATTACKMOVE:
							switch (cDir) {
							case 1: dY--; break;
							case 2: dY--; dX++; break;
							case 3: dX++; break;
							case 4: dX++; dY++; break;
							case 5: dY++; break;
							case 6: dX--; dY++; break;
							case 7: dX--; break;
							case 8: dX--; dY--; break;
							}
							break;
						default:
							break;
						}
					}
					if ((wObjectID != (WORD)m_pGame->m_pPlayer->m_sPlayerObjectID)
						&& (m_pData[dX][dY].m_sOwnerType != 0) && (m_pData[dX][dY].m_wObjectID != wObjectID))
					{
						m_pGame->RequestFullObjectData(wObjectID);
						std::memset(pName, 0, strlen(pName));
						return false;
					}
					iChatIndex = m_pData[iX][iY].m_iChatMsg;
					if (sAction != DEF_OBJECTNULLACTION) {
						sType = m_pData[iX][iY].m_sOwnerType;
						localAppearance = m_pData[iX][iY].m_appearance;
						localStatus = m_pData[iX][iY].m_status;
						iEffectType = m_pData[iX][iY].m_iEffectType;
						iEffectFrame = m_pData[iX][iY].m_iEffectFrame;
						iEffectTotalFrame = m_pData[iX][iY].m_iEffectTotalFrame;
					}
					std::memset(cTmpName, 0, sizeof(cTmpName));
					memcpy(cTmpName, m_pData[iX][iY].m_cOwnerName, 10);
					std::memset(pName, 0, sizeof(pName));
					memcpy(pName, m_pData[iX][iY].m_cOwnerName, 10);
					m_pData[iX][iY].m_wObjectID = 0; //-1; v1.41
					m_pData[iX][iY].m_iChatMsg = 0;
					m_pData[iX][iY].m_sOwnerType = 0;
					m_pData[iX][iY].m_iEffectType = 0;
					std::memset(m_pData[iX][iY].m_cOwnerName, 0, sizeof(m_pData[iX][iY].m_cOwnerName));
					m_iObjectIDcacheLocX[wObjectID] = dX + m_sPivotX;
					m_iObjectIDcacheLocY[wObjectID] = dY + m_sPivotY;
					goto EXIT_SEARCH_LOOP;
				}
				if (m_pData[iX][iY].m_wDeadObjectID == wObjectID)
				{
					dX = iX;
					dY = iY;
					if (bUseAbsPos) {
						dX = sX - m_sPivotX;
						dY = sY - m_sPivotY;
					}
					else {
						switch (sAction) {
						case DEF_OBJECTMOVE:
						case DEF_OBJECTRUN:
						case DEF_OBJECTDAMAGEMOVE:
						case DEF_OBJECTATTACKMOVE:
							switch (cDir) {
							case 1: dY--; break;
							case 2: dY--; dX++; break;
							case 3: dX++; break;
							case 4: dX++; dY++; break;
							case 5: dY++; break;
							case 6: dX--; dY++; break;
							case 7: dX--; break;
							case 8: dX--; dY--; break;
							}
							break;
						default:
							break;
						}
					}
					if ((wObjectID != (WORD)m_pGame->m_pPlayer->m_sPlayerObjectID) &&
						(m_pData[dX][dY].m_sOwnerType != 0) && (m_pData[dX][dY].m_wObjectID != wObjectID))
					{
						m_pGame->RequestFullObjectData(wObjectID);
						std::memset(pName, 0, strlen(pName));
						return false;
					}
					iChatIndex = m_pData[iX][iY].m_iDeadChatMsg;
					if (sAction != DEF_OBJECTNULLACTION) {
						sType = m_pData[iX][iY].m_sDeadOwnerType;
						localAppearance = m_pData[iX][iY].m_deadAppearance;
						localStatus = m_pData[iX][iY].m_deadStatus;
					}
					std::memset(cTmpName, 0, sizeof(cTmpName));
					memcpy(cTmpName, m_pData[iX][iY].m_cDeadOwnerName, 10);
					std::memset(pName, 0, sizeof(pName));
					memcpy(pName, m_pData[iX][iY].m_cDeadOwnerName, 10);
					m_pData[iX][iY].m_wDeadObjectID = 0; //-1; v1.41
					m_pData[iX][iY].m_iDeadChatMsg = 0;
					m_pData[iX][iY].m_sDeadOwnerType = 0;
					m_pData[iX][iY].m_iEffectType = 0;
					std::memset(m_pData[iX][iY].m_cDeadOwnerName, 0, sizeof(m_pData[iX][iY].m_cDeadOwnerName));
					m_iObjectIDcacheLocX[wObjectID] = -1 * (dX + m_sPivotX);
					m_iObjectIDcacheLocY[wObjectID] = -1 * (dY + m_sPivotY);
					goto EXIT_SEARCH_LOOP;
				}
			}
		if (ShouldRequestFullData(wObjectID, sX, sY)) {
			m_pGame->RequestFullObjectData(wObjectID);
		}
		std::memset(pName, 0, strlen(pName));
		return false;
	}

EXIT_SEARCH_LOOP:;

	if (iPreLoc == 0 && m_pData[dX][dY].m_sOwnerType != 0)
	{
		if (sAction == DEF_OBJECTDYING)
		{
			dX = sX - m_sPivotX;
			dY = sY - m_sPivotY;
		}
		if (m_pData[dX][dY].m_animation.cAction == DEF_OBJECTDYING)
		{
			m_pData[dX][dY].m_wDeadObjectID = m_pData[dX][dY].m_wObjectID;
			m_pData[dX][dY].m_sDeadOwnerType = m_pData[dX][dY].m_sOwnerType;
			m_pData[dX][dY].m_cDeadDir = m_pData[dX][dY].m_animation.cDir;
			m_pData[dX][dY].m_deadAppearance = m_pData[dX][dY].m_appearance;
			m_pData[dX][dY].m_deadStatus = m_pData[dX][dY].m_status;
			m_pData[dX][dY].m_cDeadOwnerFrame = -1;
			m_pData[dX][dY].m_dwDeadOwnerTime = dwTime;
			memcpy(m_pData[dX][dY].m_cDeadOwnerName, m_pData[dX][dY].m_cOwnerName, 11);
			m_pData[dX][dY].m_iDeadChatMsg = m_pData[dX][dY].m_iChatMsg;
			m_pData[dX][dY].m_wObjectID = 0;
			m_pData[dX][dY].m_sOwnerType = 0;
			m_pData[dX][dY].m_iChatMsg = 0;
			std::memset(m_pData[dX][dY].m_cOwnerName, 0, sizeof(m_pData[dX][dY].m_cOwnerName));
			m_iObjectIDcacheLocX[m_pData[dX][dY].m_wDeadObjectID] = -1 * m_iObjectIDcacheLocX[m_pData[dX][dY].m_wDeadObjectID];//dX; // v1.4
			m_iObjectIDcacheLocY[m_pData[dX][dY].m_wDeadObjectID] = -1 * m_iObjectIDcacheLocY[m_pData[dX][dY].m_wDeadObjectID];//dY;

			if (m_pData[dX][dY].m_iEffectType != 0)
			{
				m_pData[dX][dY].m_iEffectType = 0;
				m_pData[dX][dY].m_iEffectFrame = 0;
				m_pData[dX][dY].m_iEffectTotalFrame = 0;
				m_pData[dX][dY].m_dwEffectTime = 0;
			}
		}
	}

	if (m_pData[dX][dY].m_sOwnerType != 0)
	{
		if ((wObjectID != (WORD)m_pGame->m_pPlayer->m_sPlayerObjectID)
			&& (m_pData[dX][dY].m_wObjectID == (WORD)m_pGame->m_pPlayer->m_sPlayerObjectID))
		{
			m_pGame->RequestFullObjectData(wObjectID);
			return false;
		}
		else
		{
			return false;
		}
	}

	if (iPreLoc == 0)
	{
		m_pData[dX][dY].m_wObjectID = wObjectID;
		m_pData[dX][dY].m_sOwnerType = sType;
		m_pData[dX][dY].m_animation.cDir = cDir;
		m_pData[dX][dY].m_appearance = localAppearance;
		m_pData[dX][dY].m_status = localStatus;
		m_pData[dX][dY].m_sV1 = sV1;
		m_pData[dX][dY].m_sV2 = sV2;
		m_pData[dX][dY].m_sV3 = sV3;
		m_pData[dX][dY].m_iEffectType = iEffectType;
		m_pData[dX][dY].m_iEffectFrame = iEffectFrame;
		m_pData[dX][dY].m_iEffectTotalFrame = iEffectTotalFrame;
		std::memset(m_pData[dX][dY].m_cOwnerName, 0, sizeof(m_pData[dX][dY].m_cOwnerName));
		strncpy_s(m_pData[dX][dY].m_cOwnerName, sizeof(m_pData[dX][dY].m_cOwnerName), cTmpName, _TRUNCATE);
		if ((sAction != DEF_OBJECTNULLACTION) && (sAction != DEF_MSGTYPE_CONFIRM) && (sAction != DEF_MSGTYPE_REJECT))
		{
			// Look up animation definition: players use PlayerAnim, NPCs use m_stFrame
			int16_t maxFrame, frameTime;
			bool loop;
			if (hb::owner::IsPlayer(sType)) {
				const AnimDef& def = PlayerAnim::FromAction(static_cast<int8_t>(sAction));
				maxFrame = def.sMaxFrame;
				frameTime = def.sFrameTime;
				loop = def.bLoop;
			} else {
				maxFrame = m_stFrame[sType][sAction].m_sMaxFrame;
				frameTime = m_stFrame[sType][sAction].m_sFrameTime;
				loop = false; // All actions are one-shot; overflow triggers STOP transition + command unlock
			}
			m_pData[dX][dY].m_animation.SetAction(static_cast<int8_t>(sAction), cDir,
				maxFrame, frameTime, loop, static_cast<int8_t>(iFrame));
			m_pData[dX][dY].m_animation.dwLastFrameTime = dwTime;

			// Initialize smooth movement interpolation for movement actions
			if (sAction == DEF_OBJECTMOVE || sAction == DEF_OBJECTRUN ||
				sAction == DEF_OBJECTDAMAGEMOVE || sAction == DEF_OBJECTATTACKMOVE)
			{
				bool hasHaste = localStatus.bHaste;
				bool isFrozen = localStatus.bFrozen;
				uint32_t duration = EntityMotion::GetDurationForAction(sAction, hasHaste, isFrozen);

				if (m_pData[dX][dY].m_motion.IsMoving())
				{
					// Entity still interpolating previous tile — queue this move
					m_pData[dX][dY].m_motion.QueueMove(cDir, duration);
				}
				else if (bHadOldMotion && cOldMotionDir == cDir)
				{
					// Seamless tile transition: only when continuing in the SAME direction
					// When transitioning from old tile at offset (e.g., -1.6) to new tile,
					// the new offset should be: oldOffset + stdStart
					// This maintains visual continuity across tile boundaries
					int16_t stdStartX, stdStartY;
					EntityMotion::GetDirectionStartOffset(cDir, stdStartX, stdStartY);

					// Calculate the continuous offset for the new tile
					// newOffset = oldOffset + stdStart (since stdStart is negative, this extends the range)
					float newOffsetX = fOldMotionOffsetX + static_cast<float>(stdStartX);
					float newOffsetY = fOldMotionOffsetY + static_cast<float>(stdStartY);

					m_pData[dX][dY].m_motion.StartMoveWithOffset(cDir, dwTime, duration, newOffsetX, newOffsetY);
				}
				else
				{
					m_pData[dX][dY].m_motion.StartMove(cDir, dwTime, duration);
				}
			}
		}
		else
		{
			// NULLACTION/CONFIRM/REJECT: initialize with STOP animation if not already set
			if (m_pData[dX][dY].m_animation.sMaxFrame == 0)
			{
				int16_t maxFrame, frameTime;
				if (hb::owner::IsPlayer(sType)) {
					maxFrame = PlayerAnim::Stop.sMaxFrame;
					frameTime = PlayerAnim::Stop.sFrameTime;
				} else {
					maxFrame = m_stFrame[sType][DEF_OBJECTSTOP].m_sMaxFrame;
					frameTime = m_stFrame[sType][DEF_OBJECTSTOP].m_sFrameTime;
				}
				m_pData[dX][dY].m_animation.SetAction(DEF_OBJECTSTOP, cDir,
					maxFrame, frameTime, false);
			}
			m_pData[dX][dY].m_animation.dwLastFrameTime = dwTime;
		}
		m_pData[dX][dY].m_iChatMsg = iChatIndex;
		if (localAppearance.iEffectType != 0)
		{
			m_pData[dX][dY].m_iEffectType = localAppearance.iEffectType;
			if (sAction == DEF_OBJECTNULLACTION)
			{
				m_pData[dX][dY].m_iEffectFrame = 0;
				m_pData[dX][dY].m_dwEffectTime = dwTime;
			}
			switch (m_pData[dX][dY].m_iEffectType) {
			case 1: m_pData[dX][dY].m_iEffectTotalFrame = 13; break;
			case 2: m_pData[dX][dY].m_iEffectTotalFrame = 11; break;
			}
		}
		else
		{
			m_pData[dX][dY].m_iEffectType = 0;
		}
	}
	else // iPreLoc == 1
	{
		m_pData[dX][dY].m_wDeadObjectID = wObjectID;
		m_pData[dX][dY].m_sDeadOwnerType = sType;
		m_pData[dX][dY].m_cDeadDir = cDir;
		m_pData[dX][dY].m_deadAppearance = localAppearance;
		m_pData[dX][dY].m_deadStatus = localStatus;
		std::memset(m_pData[dX][dY].m_cDeadOwnerName, 0, sizeof(m_pData[dX][dY].m_cDeadOwnerName));
		strncpy_s(m_pData[dX][dY].m_cDeadOwnerName, sizeof(m_pData[dX][dY].m_cDeadOwnerName), cTmpName, _TRUNCATE);
		m_pData[dX][dY].m_dwDeadOwnerTime = dwTime;
		m_pData[dX][dY].m_iDeadChatMsg = iChatIndex;
		if (localAppearance.iEffectType != 0)
		{
			m_pData[dX][dY].m_iEffectType = localAppearance.iEffectType;
			if (sAction == DEF_OBJECTNULLACTION)
			{
				m_pData[dX][dY].m_iEffectFrame = 0;
				m_pData[dX][dY].m_dwEffectTime = dwTime;
			}
			switch (m_pData[dX][dY].m_iEffectType) {
			case 1: m_pData[dX][dY].m_iEffectTotalFrame = 13; break;
			case 2: m_pData[dX][dY].m_iEffectTotalFrame = 11; break;
			}
		}
		else
		{
			m_pData[dX][dY].m_iEffectType = 0;
		}
	}
	return true;
}






int CMapData::iObjectFrameCounter(char* cPlayerName, short sViewPointX, short sViewPointY)
{
	int dX, dY, sVal;
	uint32_t dwTime, dwRealTime, dwFrameTime;
	int  iDelay;
	int  iRet, iSoundIndex, iSkipFrame;
	int  cDir, cTotalFrame, cFrameMoveDots;
	static DWORD S_dwUpdateTime = GameClock::GetTimeMS();
	int   sWeaponType, sCenterX, sCenterY, sDist;//, sAbsX, sAbsY;
	bool  bAutoUpdate = false, dynObjsNeedUpdate = false;
	short dx, dy;
	long  lPan;

	iRet = 0;
	dwTime = dwRealTime = GameClock::GetTimeMS();
	if ((dwTime - m_dwFrameTime) >= 1)
		m_dwFrameTime = dwTime;

	sVal = sViewPointX - (m_sPivotX * 32);
	sCenterX = (sVal / 32) + VIEW_CENTER_TILE_X();
	sVal = sViewPointY - (m_sPivotY * 32);
	sCenterY = (sVal / 32) + VIEW_CENTER_TILE_Y();
	m_sRectX = m_pGame->m_sVDL_X - m_sPivotX;
	m_sRectY = m_pGame->m_sVDL_Y - m_sPivotY;

	dynObjsNeedUpdate = (dwTime - m_dwDOframeTime) > 100;
	bAutoUpdate = (dwTime - S_dwUpdateTime) > 40;

	// PERFORMANCE OPTIMIZATION: Only process tiles near player's view
	// Screen is ~LOGICAL_WIDTHxLOGICAL_HEIGHT pixels = ~20x15 tiles, add buffer for effects
	// OLD: Processed all 60x55 = 3300 tiles every frame
	// NEW: Process only ~35x30 = 1050 tiles (68% reduction)
	int halfViewX = VIEW_TILE_WIDTH() / 2;
	int halfViewY = VIEW_TILE_HEIGHT() / 2;
	int bufferX = 5;
	int bufferY = 6;
	int startX = sCenterX - (halfViewX + bufferX);
	int endX = sCenterX + (halfViewX + bufferX + 1);
	int startY = sCenterY - (halfViewY + bufferY);
	int endY = sCenterY + (halfViewY + bufferY);
	if (startX < 0) startX = 0;
	if (startY < 0) startY = 0;
	if (endX > MAPDATASIZEX) endX = MAPDATASIZEX;
	if (endY > MAPDATASIZEY) endY = MAPDATASIZEY;

	for (dX = startX; dX < endX; dX++)
		for (dY = startY; dY < endY; dY++)
		{
			sDist = (abs(sCenterX - dX) + abs(sCenterY - dY)) / 2;
			lPan = halfViewX > 0 ? ((dX - sCenterX) * 100) / halfViewX : 0;

			// Dynamic Object
			if (dynObjsNeedUpdate)//00496B99  JBE 00496F43
			{
				m_pData[dX][dY].m_iEffectFrame++;
				switch (m_pData[dX][dY].m_iEffectType) {
				case 1:
					if (m_pData[dX][dY].m_iEffectTotalFrame < m_pData[dX][dY].m_iEffectFrame)
						m_pData[dX][dY].m_iEffectFrame = 4;
					break;
				case 2:
					if (m_pData[dX][dY].m_iEffectTotalFrame < m_pData[dX][dY].m_iEffectFrame)
						m_pData[dX][dY].m_iEffectFrame = 3;
					break;
				}
				if ((m_pData[dX][dY].m_sDynamicObjectType != 0))
				{
					m_pData[dX][dY].m_cDynamicObjectFrame++;
					switch (m_pData[dX][dY].m_sDynamicObjectType) {
					case DEF_DYNAMICOBJECT_SPIKE:
						if (m_pData[dX][dY].m_cDynamicObjectFrame >= 13)
							m_pData[dX][dY].m_cDynamicObjectFrame = 0;
						break;

					case DEF_DYNAMICOBJECT_ICESTORM:
						if (m_pData[dX][dY].m_cDynamicObjectFrame >= 10)
							m_pData[dX][dY].m_cDynamicObjectFrame = 0;
						//if (m_pData[dX][dY].m_cDynamicObjectFrame == 1)
						//	m_pGame->PlaySound('E', 16, sDist);
						break;

					case DEF_DYNAMICOBJECT_FIRE:// Firewall
					case DEF_DYNAMICOBJECT_FIRE3: // by Snoopy(FireBow)
						if (m_pData[dX][dY].m_cDynamicObjectFrame >= 24)
							m_pData[dX][dY].m_cDynamicObjectFrame = 0;
						if (m_pData[dX][dY].m_cDynamicObjectFrame == 1)
						{
							m_pGame->PlaySound('E', 9, sDist);
						}
						break;

					case DEF_DYNAMICOBJECT_FIRE2:	//  // Crusade buildings burning.
						if (m_pData[dX][dY].m_cDynamicObjectFrame > 27)
							m_pData[dX][dY].m_cDynamicObjectFrame = 0;
						if (m_pData[dX][dY].m_cDynamicObjectFrame == 1)
						{
							m_pGame->PlaySound('E', 9, sDist);
						}
						if ((m_pData[dX][dY].m_cDynamicObjectFrame % 6) == 0)
						{
							m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + (rand() % 10 - 5) + 5, (m_sPivotY + dY) * 32, 0, 0, 0, 0);
							m_pGame->m_pEffectManager->AddEffect(EffectType::MS_FIRE_SMOKE, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0, 0);
						}
						break;

					case DEF_DYNAMICOBJECT_FISHOBJECT:
						if ((rand() % 12) == 1)
							m_pGame->m_pEffectManager->AddEffect(EffectType::BUBBLES_DRUNK, (m_sPivotX + dX) * 32 + m_pData[dX][dY].m_cDynamicObjectData1, (m_sPivotY + dY) * 32 + m_pData[dX][dY].m_cDynamicObjectData2, 0, 0, 0);
						break;

					case DEF_DYNAMICOBJECT_FISH:
						if ((dwTime - m_pData[dX][dY].m_dwDynamicObjectTime) < 100) break;
						m_pData[dX][dY].m_dwDynamicObjectTime = dwTime;
						if (m_pData[dX][dY].m_cDynamicObjectFrame >= 15) m_pData[dX][dY].m_cDynamicObjectFrame = 0;
						if ((rand() % 15) == 1) m_pGame->m_pEffectManager->AddEffect(EffectType::BUBBLES_DRUNK, (m_sPivotX + dX) * 32 + m_pData[dX][dY].m_cDynamicObjectData1, (m_sPivotY + dY) * 32 + m_pData[dX][dY].m_cDynamicObjectData2, 0, 0, 0);
						cDir = CMisc::cGetNextMoveDir(m_pData[dX][dY].m_cDynamicObjectData1, m_pData[dX][dY].m_cDynamicObjectData2, 0, 0);
						switch (cDir) {
						case 1:
							m_pData[dX][dY].m_cDynamicObjectData4 -= 2;
							break;
						case 2:
							m_pData[dX][dY].m_cDynamicObjectData4 -= 2;
							m_pData[dX][dY].m_cDynamicObjectData3 += 2;
							break;
						case 3:
							m_pData[dX][dY].m_cDynamicObjectData3 += 2;
							break;
						case 4:
							m_pData[dX][dY].m_cDynamicObjectData3 += 2;
							m_pData[dX][dY].m_cDynamicObjectData4 += 2;
							break;
						case 5:
							m_pData[dX][dY].m_cDynamicObjectData4 += 2;
							break;
						case 6:
							m_pData[dX][dY].m_cDynamicObjectData3 -= 2;
							m_pData[dX][dY].m_cDynamicObjectData4 += 2;
							break;
						case 7:
							m_pData[dX][dY].m_cDynamicObjectData3 -= 2;
							break;
						case 8:
							m_pData[dX][dY].m_cDynamicObjectData3 -= 2;
							m_pData[dX][dY].m_cDynamicObjectData4 -= 2;
							break;
						}

						if (m_pData[dX][dY].m_cDynamicObjectData3 < -12) m_pData[dX][dY].m_cDynamicObjectData3 = -12;
						if (m_pData[dX][dY].m_cDynamicObjectData3 > 12) m_pData[dX][dY].m_cDynamicObjectData3 = 12;
						if (m_pData[dX][dY].m_cDynamicObjectData4 < -12) m_pData[dX][dY].m_cDynamicObjectData4 = -12;
						if (m_pData[dX][dY].m_cDynamicObjectData4 > 12) m_pData[dX][dY].m_cDynamicObjectData4 = 12;

						m_pData[dX][dY].m_cDynamicObjectData1 += m_pData[dX][dY].m_cDynamicObjectData3;
						m_pData[dX][dY].m_cDynamicObjectData2 += m_pData[dX][dY].m_cDynamicObjectData4;
						break;

					case DEF_DYNAMICOBJECT_PCLOUD_BEGIN:
						if (m_pData[dX][dY].m_cDynamicObjectFrame >= 8)
						{
							m_pData[dX][dY].m_sDynamicObjectType = DEF_DYNAMICOBJECT_PCLOUD_LOOP;
							m_pData[dX][dY].m_cDynamicObjectFrame = rand() % 8;
						}
						break;

					case DEF_DYNAMICOBJECT_PCLOUD_LOOP:
						if (m_pData[dX][dY].m_cDynamicObjectFrame >= 8)
						{
							m_pData[dX][dY].m_cDynamicObjectFrame = 0;
						}
						break;

					case DEF_DYNAMICOBJECT_PCLOUD_END:
						if (m_pData[dX][dY].m_cDynamicObjectFrame >= 8)
						{
							m_pData[dX][dY].m_sDynamicObjectType = 0;
						}
						break;

					case DEF_DYNAMICOBJECT_ARESDENFLAG1:
						if (m_pData[dX][dY].m_cDynamicObjectFrame >= 4)
						{
							m_pData[dX][dY].m_cDynamicObjectFrame = 0;
						}
						break;

					case DEF_DYNAMICOBJECT_ELVINEFLAG1:
						if (m_pData[dX][dY].m_cDynamicObjectFrame >= 8)
						{
							m_pData[dX][dY].m_cDynamicObjectFrame = 4;
						}
						break;
					}
				}
			}

			// Dead think 00496F43
			if (m_pData[dX][dY].m_sDeadOwnerType != 0) //00496F62  JE SHORT 00496FD8
				if ((m_pData[dX][dY].m_cDeadOwnerFrame >= 0) && ((dwTime - m_pData[dX][dY].m_dwDeadOwnerTime) > 150))
				{
					m_pData[dX][dY].m_dwDeadOwnerTime = dwTime;
					m_pData[dX][dY].m_cDeadOwnerFrame++;
					if (iRet == 0)
					{
						iRet = -1;
						S_dwUpdateTime = dwTime;
					}
					if (m_pData[dX][dY].m_cDeadOwnerFrame > 10)
					{
						m_pData[dX][dY].m_wDeadObjectID = 0;
						m_pData[dX][dY].m_sDeadOwnerType = 0;
						std::memset(m_pData[dX][dY].m_cDeadOwnerName, 0, sizeof(m_pData[dX][dY].m_cDeadOwnerName));
					}
				}

			// Alive thing 00496FD8
			if (m_pData[dX][dY].m_sOwnerType != 0)
			{
				// Get base frame time from source (not the already-modified sFrameTime)
				int16_t baseFrameTime;
				short ownerType = m_pData[dX][dY].m_sOwnerType;
				int8_t ownerAction = m_pData[dX][dY].m_animation.cAction;
				if (hb::owner::IsPlayer(ownerType)) {
					baseFrameTime = PlayerAnim::FromAction(ownerAction).sFrameTime;
				} else {
					baseFrameTime = m_stFrame[ownerType][ownerAction].m_sFrameTime;
				}

				// Compute effective frame time with status modifiers
				switch (ownerAction) {
				case DEF_OBJECTATTACK: // 3
				case DEF_OBJECTATTACKMOVE:	// 8
					iDelay = m_pData[dX][dY].m_status.iAttackDelay * 12;
					break;
				case DEF_OBJECTMAGIC: // 4
					if (m_pGame->m_pPlayer->m_iSkillMastery[4] == 100) iDelay = -17;
					else iDelay = 0;
					break;
				default:
					iDelay = 0;
					break;
				}
				// v1.42 Frozen
				if (m_pData[dX][dY].m_status.bFrozen)
					iDelay += baseFrameTime >> 2;

				if (m_pData[dX][dY].m_status.bHaste) { // haste
					int16_t runFrameTime = hb::owner::IsPlayer(ownerType)
						? PlayerAnim::Run.sFrameTime
						: m_stFrame[ownerType][DEF_OBJECTRUN].m_sFrameTime;
					iDelay -= static_cast<int>(runFrameTime / 2.3);
				}

				// Apply computed delay to animation state
				dwFrameTime = baseFrameTime + iDelay;
				m_pData[dX][dY].m_animation.sFrameTime = static_cast<int16_t>(dwFrameTime);

				if (m_pData[dX][dY].m_animation.Update(dwTime))
				{
					if (iRet == 0)
					{
						iRet = -1;
						S_dwUpdateTime = dwTime;
					}
					if (strncmp(m_pData[dX][dY].m_cOwnerName, cPlayerName, 10) == 0)
					{
						iRet = 1;
						S_dwUpdateTime = dwTime;
						if ((dwRealTime - m_dwFrameCheckTime) > dwFrameTime)
							m_dwFrameAdjustTime = ((dwRealTime - m_dwFrameCheckTime) - dwFrameTime);
						m_dwFrameCheckTime = dwRealTime;
					}
					if (m_pData[dX][dY].m_animation.IsFinished())
					{
						if ((m_sRectX <= dX) && ((m_sRectX + 25) >= dX)
							&& (m_sRectY <= dY) && ((m_sRectY + 19) >= dY))
							// (!) Ower -> DeadOwner 004971AB
						{
							if (m_pData[dX][dY].m_animation.cAction == DEF_OBJECTDYING) //10
							{
								m_pData[dX][dY].m_wDeadObjectID = m_pData[dX][dY].m_wObjectID;
								m_pData[dX][dY].m_sDeadOwnerType = m_pData[dX][dY].m_sOwnerType;
								m_pData[dX][dY].m_cDeadDir = m_pData[dX][dY].m_animation.cDir;
								m_pData[dX][dY].m_deadAppearance = m_pData[dX][dY].m_appearance;
								m_pData[dX][dY].m_deadStatus = m_pData[dX][dY].m_status;
								m_pData[dX][dY].m_iDeadChatMsg = m_pData[dX][dY].m_iChatMsg; // v1.411
								m_pData[dX][dY].m_cDeadOwnerFrame = -1;
								memcpy(m_pData[dX][dY].m_cDeadOwnerName, m_pData[dX][dY].m_cOwnerName, 11);
								m_pData[dX][dY].m_wObjectID = 0;
								m_pData[dX][dY].m_sOwnerType = 0;
								std::memset(m_pData[dX][dY].m_cOwnerName, 0, sizeof(m_pData[dX][dY].m_cOwnerName));
								m_iObjectIDcacheLocX[m_pData[dX][dY].m_wDeadObjectID] = -1 * m_iObjectIDcacheLocX[m_pData[dX][dY].m_wDeadObjectID]; //dX; // v1.4
								m_iObjectIDcacheLocY[m_pData[dX][dY].m_wDeadObjectID] = -1 * m_iObjectIDcacheLocY[m_pData[dX][dY].m_wDeadObjectID]; //dY;
							}
							else
							{
								// Transition to STOP: use player or NPC anim defs
								int16_t stopMaxFrame, stopFrameTime;
								bool stopLoop;
								if (hb::owner::IsPlayer(m_pData[dX][dY].m_sOwnerType)) {
									const AnimDef& def = PlayerAnim::FromAction(DEF_OBJECTSTOP);
									stopMaxFrame = def.sMaxFrame;
									stopFrameTime = def.sFrameTime;
									stopLoop = def.bLoop;
								} else {
									stopMaxFrame = m_stFrame[m_pData[dX][dY].m_sOwnerType][DEF_OBJECTSTOP].m_sMaxFrame;
									stopFrameTime = m_stFrame[m_pData[dX][dY].m_sOwnerType][DEF_OBJECTSTOP].m_sFrameTime;
									stopLoop = false;
								}
								m_pData[dX][dY].m_animation.SetAction(DEF_OBJECTSTOP,
									m_pData[dX][dY].m_animation.cDir,
									stopMaxFrame, stopFrameTime, stopLoop);
								m_pData[dX][dY].m_animation.dwLastFrameTime = dwTime;
							}
							if (strncmp(m_pData[dX][dY].m_cOwnerName, cPlayerName, 10) == 0)
							{
								iRet = 2;
								S_dwUpdateTime = dwTime;
							}
						}
						else
						{
							m_pData[dX][dY].m_wObjectID = 0;
							m_pData[dX][dY].m_sOwnerType = 0;
							std::memset(m_pData[dX][dY].m_cOwnerName, 0, sizeof(m_pData[dX][dY].m_cOwnerName));
							if (m_pGame->m_pChatMsgList[m_pData[dX][dY].m_iChatMsg])
							{
								m_pGame->m_pChatMsgList[m_pData[dX][dY].m_iChatMsg].reset();
							}
						}
					}
					if (m_pData[dX][dY].m_animation.cAction == DEF_OBJECTSTOP) { // DEF_OBJECTSTOP = 1 // 00497334
						switch (m_pData[dX][dY].m_sOwnerType) {
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
						case 6: // glowing armor/weapon
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1) || (m_pData[dX][dY].m_animation.cCurrentFrame == 5))
							{
								if ((((m_pData[dX][dY].m_appearance.iWeaponGlare | m_pData[dX][dY].m_appearance.iShieldGlare) != 0) || (m_pData[dX][dY].m_status.bGMMode)) && (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 20 - 10), (m_sPivotY + dY) * 32 - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
								}
								// Snoopy: Angels
								if (((m_pData[dX][dY].m_status.iAngelPercent) > rand() % 6) // Angel stars
									&& (m_pData[dX][dY].m_status.HasAngelType())
									&& (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 15 + 10), (m_sPivotY + dY) * 32 - (rand() % 30) - 50, 0, 0, -(rand() % 8), 0);
								}
							}
							break;
						case hb::owner::EnergyShield: // ESG
						case hb::owner::GrandMagicGenerator: // GMG
						case hb::owner::ManaStone: // ManaStone
							if ((rand() % 40) == 25)
							{
								m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 60 - 30), (m_sPivotY + dY) * 32 - (rand() % 100) - 5, 0, 0, -(rand() % 12), 0);
								m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 60 - 30), (m_sPivotY + dY) * 32 - (rand() % 100) - 5, 0, 0, -(rand() % 12), 0);
								m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 60 - 30), (m_sPivotY + dY) * 32 - (rand() % 100) - 5, 0, 0, -(rand() % 12), 0);
								m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 60 - 30), (m_sPivotY + dY) * 32 - (rand() % 100) - 5, 0, 0, -(rand() % 12), 0);
							}
							break;
						case hb::owner::IceGolem: // IceGolem
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 3)
							{
								m_pGame->m_pEffectManager->AddEffect(EffectType::ICE_GOLEM_EFFECT_1, (m_sPivotX + dX) * 32 + (rand() % 40 - 20), (m_sPivotY + dY) * 32 + (rand() % 40 - 20), 0, 0, 0);
								m_pGame->m_pEffectManager->AddEffect(EffectType::ICE_GOLEM_EFFECT_1, (m_sPivotX + dX) * 32 + (rand() % 40 - 20), (m_sPivotY + dY) * 32 + (rand() % 40 - 20), 0, 0, 0);
							}
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
							{
								m_pGame->m_pEffectManager->AddEffect(EffectType::ICE_GOLEM_EFFECT_2, (m_sPivotX + dX) * 32 + (rand() % 40 - 20), (m_sPivotY + dY) * 32 + (rand() % 40 - 20), 0, 0, 0);
								m_pGame->m_pEffectManager->AddEffect(EffectType::ICE_GOLEM_EFFECT_2, (m_sPivotX + dX) * 32 + (rand() % 40 - 20), (m_sPivotY + dY) * 32 + (rand() % 40 - 20), 0, 0, 0);
							}
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
							{
								m_pGame->m_pEffectManager->AddEffect(EffectType::ICE_GOLEM_EFFECT_3, (m_sPivotX + dX) * 32 + (rand() % 40 - 20), (m_sPivotY + dY) * 32 + (rand() % 40 - 20), 0, 0, 0);
								m_pGame->m_pEffectManager->AddEffect(EffectType::ICE_GOLEM_EFFECT_3, (m_sPivotX + dX) * 32 + (rand() % 40 - 20), (m_sPivotY + dY) * 32 + (rand() % 40 - 20), 0, 0, 0);
							}
							break;
						}
					}

					if (m_pData[dX][dY].m_animation.cAction == DEF_OBJECTMOVE) { //2 //004977BF
						switch (m_pData[dX][dY].m_sOwnerType) {
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
						case 6:
						case hb::owner::TempleKnight: // TK
						case hb::owner::Beholder: // Beholder
						case hb::owner::DarkElf: // Dark-Elf
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1) || (m_pData[dX][dY].m_animation.cCurrentFrame == 5))
							{
								m_pGame->PlaySound('C', 8, sDist, lPan);
								if ((((m_pData[dX][dY].m_appearance.iWeaponGlare | m_pData[dX][dY].m_appearance.iShieldGlare) != 0) || (m_pData[dX][dY].m_status.bGMMode)) && (!m_pData[dX][dY].m_status.bInvisibility))
								{
									cTotalFrame = 8;
									cFrameMoveDots = 32 / cTotalFrame;
									dx = dy = 0;
									switch (m_pData[dX][dY].m_animation.cDir) {
									case 1: dy = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
									case 2: dy = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dx = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
									case 3: dx = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
									case 4: dx = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dy = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
									case 5: dy = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
									case 6: dy = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dx = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
									case 7: dx = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
									case 8: dx = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dy = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
									}
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + dx + (rand() % 20 - 10), (m_sPivotY + dY) * 32 + dy - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + dx + (rand() % 20 - 10), (m_sPivotY + dY) * 32 + dy - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
								}
								// Snoopy: Angels
								if (((m_pData[dX][dY].m_status.iAngelPercent) > rand() % 6) // Angel stars
									&& (m_pData[dX][dY].m_status.HasAngelType())
									&& (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 15 + 10), (m_sPivotY + dY) * 32 - (rand() % 30) - 50, 0, 0, -(rand() % 8), 0);
								}
							}
							break;

						case hb::owner::Sorceress: // Snoopy: Sorceress
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 149, sDist, lPan);
							break;

						case hb::owner::ATK: // Snoopy: ATK
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 142, sDist, lPan);
							break;

						case hb::owner::MasterElf: // Snoopy: MasterElf
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
							{
								if (true) m_pGame->PlaySound('C', 10, sDist, lPan);
							}
							break;

						case hb::owner::DSK: // Snoopy: DSK
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 147, sDist, lPan);
							break;

						case hb::owner::Slime: // Slime
						case hb::owner::TigerWorm: // TW
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 1, sDist, lPan);
							break;

						case hb::owner::Skeleton: // SKel
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 13, sDist, lPan);
							break;

						case hb::owner::Cyclops: // Cyclops
						case hb::owner::HellClaw: // HC
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 41, sDist, lPan);
							break;

						case hb::owner::OrcMage: // Orc
						case hb::owner::Stalker: // SK
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 9, sDist, lPan);
							break;

						case hb::owner::GiantAnt: // Ant
						case hb::owner::LightWarBeetle: // LWBeetle
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 29, sDist, lPan);
							break;

						case hb::owner::Scorpion: // Scorpion
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 21, sDist, lPan);
							break;

						case hb::owner::Zombie: // Zombie
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 17, sDist, lPan);
							break;

						case hb::owner::Amphis: // Snake
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 25, sDist, lPan);
							break;

						case hb::owner::ClayGolem: // Clay-Golem
						case hb::owner::Gargoyle: // Gargoyle
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 37, sDist, lPan);
							break;

						case hb::owner::Hellhound: // HH
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 5, sDist, lPan);
							break;

						case hb::owner::Troll: // Troll
						case hb::owner::Minaus: // Snoopy: Ajout Minaus
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 46, sDist, lPan);
							break;

						case hb::owner::Ogre: // Ogre
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 51, sDist, lPan);
							break;

						case hb::owner::Liche: // Liche
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 55, sDist, lPan);
							break;

						case hb::owner::Demon: // DD
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 59, sDist, lPan);
							break;

						case hb::owner::Unicorn: // Uni
						case hb::owner::GodsHandKnightCK: // GHKABS
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 63, sDist, lPan);
							break;

						case hb::owner::WereWolf: // WW
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 67, sDist, lPan);
							break;

						case hb::owner::Bunny://Rabbit
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 71, sDist, lPan);
							break;

						case hb::owner::Cat://Cat
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 72, sDist, lPan);
							break;

						case hb::owner::GiantFrog://Giant-Frog
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 73, sDist, lPan);
							break;

						case hb::owner::MountainGiant://Mountain Giant
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 87, sDist, lPan);
							break;

						case hb::owner::Ettin://Ettin
						case hb::owner::MasterOrc: // Snoopy: MasterMageOrc
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 91, sDist, lPan);
							break;

						case hb::owner::CannibalPlant://Cannibal Plant
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 95, sDist, lPan);
							break;

						case hb::owner::Rudolph://Rudolph
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('C', 11, sDist, lPan);
							break;

						case hb::owner::DireBoar: // DireBoar
						case hb::owner::GiantCrayfish: // Snoopy: GiantCrayFish
						case hb::owner::Barbarian: // Snoopy: Barbarian
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 87, sDist, lPan);
							break;

						case hb::owner::Frost://Frost
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 25, sDist, lPan);
							break;

						case hb::owner::StoneGolem: // Stone-Golem
						case hb::owner::BattleGolem: // BG
						case hb::owner::IceGolem: // Snoopy: IceGolem
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 33, sDist, lPan);
							break;
							/*	case hb::owner::IceGolem: // IceGolem particulier a la v2.20, absent de la v3.51
								if ( (m_pData[dX][dY].m_animation.cCurrentFrame == 1) )
								{	m_pGame->PlaySound('M', 33, sDist, lPan);
									switch (m_pData[dX][dY].m_animation.cDir) {
									case 1 : dx = 0; dy = -1; break;
									case 2 : dy = -1; dx = 1; break;
									case 3 : dx = 1; dy = 0; break;
									case 4 : dx = 1; dy = 1; break;
									case 5 : dx = 0; dy = 1; break;
									case 6 : dy = 1; dx = -1; break;
									case 7 : dx = -1; dy = 0; break;
									case 8 : dx = -1; dy = -1; break;
									}
									//m_pGame->m_pEffectManager->AddEffect( 75, (m_sPivotX+dX)*32, (m_sPivotY+dY)*32, dx, dy, 0 );
									//m_pGame->m_pEffectManager->AddEffect( 76, (m_sPivotX+dX)*32, (m_sPivotY+dY)*32, dx, dy, 0 );
									//m_pGame->m_pEffectManager->AddEffect( 77, (m_sPivotX+dX)*32, (m_sPivotY+dY)*32, dx, dy, 0 );
								}
								break;*/

						case hb::owner::FireWyvern: // Snoopy: Fite-Wyvern
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 106, sDist, lPan);
							break;

						case hb::owner::Tentocle: // Snoopy: Tentocle
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 110, sDist, lPan);
							break;

						case hb::owner::ClawTurtle: // Snoopy: Claw Turtle
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 114, sDist, lPan);
							break;

						case hb::owner::Centaur: // Snoopy: Centaurus
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 117, sDist, lPan);
							break;

						case hb::owner::GiTree: // Snoopy: Giant Tree
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 122, sDist, lPan);
							break;

						case hb::owner::GiLizard: // Snoopy: Giant Lizard
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 126, sDist, lPan);
							break;

						case hb::owner::Dragon: // Snoopy: Dragon
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 130, sDist, lPan);
							break;

						case hb::owner::Nizie: // Snoopy: Nizie
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 134, sDist, lPan);
							break;

						case hb::owner::Abaddon: // void CGame::DrawDruncncity();Abaddon
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 136, sDist, lPan);
							break;

						default:
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1) || (m_pData[dX][dY].m_animation.cCurrentFrame == 3))
								m_pGame->PlaySound('C', 8, sDist, lPan);
							break;
						}
					} // Fin du DEF_OBJECTMOVE

					if (m_pData[dX][dY].m_animation.cAction == DEF_OBJECTRUN)  // 2   //00497E34
					{
						switch (m_pData[dX][dY].m_sOwnerType) {
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
						case 6:
						case hb::owner::GodsHandKnight: // GHK
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1) || (m_pData[dX][dY].m_animation.cCurrentFrame == 5))
							{
								cTotalFrame = 8;
								cFrameMoveDots = 32 / cTotalFrame;
								dx = dy = 0;
								switch (m_pData[dX][dY].m_animation.cDir) {
								case 1: dy = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 2: dy = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dx = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 3: dx = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 4: dx = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dy = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 5: dy = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 6: dy = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dx = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 7: dx = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 8: dx = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dy = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								}
								if ((m_pGame->m_cWhetherEffectType >= 1) && (m_pGame->m_cWhetherEffectType <= 3))
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT_RAIN, (m_sPivotX + dX) * 32 + dx, (m_sPivotY + dY) * 32 + dy, 0, 0, 0, 0);
								else m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT, (m_sPivotX + dX) * 32 + dx, (m_sPivotY + dY) * 32 + dy, 0, 0, 0, 0);
								if ((((m_pData[dX][dY].m_appearance.iWeaponGlare | m_pData[dX][dY].m_appearance.iShieldGlare) != 0) || (m_pData[dX][dY].m_status.bGMMode)) && (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + dx + (rand() % 20 - 10), (m_sPivotY + dY) * 32 + dy - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + dx + (rand() % 20 - 10), (m_sPivotY + dY) * 32 + dy - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
								}
								// Snoopy: Angels
								if (((m_pData[dX][dY].m_status.iAngelPercent) > rand() % 6) // Angel stars
									&& (m_pData[dX][dY].m_status.HasAngelType())
									&& (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 15 + 10), (m_sPivotY + dY) * 32 - (rand() % 30) - 50, 0, 0, -(rand() % 8), 0);
								}
								m_pGame->PlaySound('C', 10, sDist, lPan);
							}
							break;
						}
					}
					if (m_pData[dX][dY].m_animation.cAction == DEF_OBJECTATTACKMOVE)  //8 //004980A5
					{
						switch (m_pData[dX][dY].m_sOwnerType) {
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
						case 6:
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 2) // vu comme case 2
							{
								if (true) m_pGame->PlaySound('C', 4, sDist); //bruit fleche
								cTotalFrame = 8;
								cFrameMoveDots = 32 / cTotalFrame;
								dx = dy = 0;
								switch (m_pData[dX][dY].m_animation.cDir) {
								case 1: dy = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 2: dy = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dx = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 3: dx = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 4: dx = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dy = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 5: dy = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 6: dy = -cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dx = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 7: dx = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								case 8: dx = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); dy = cFrameMoveDots * (cTotalFrame - m_pData[dX][dY].m_animation.cCurrentFrame); break;
								}
								if ((((m_pData[dX][dY].m_appearance.iWeaponGlare | m_pData[dX][dY].m_appearance.iShieldGlare) != 0) || (m_pData[dX][dY].m_status.bGMMode)) && (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + dx + (rand() % 20 - 10), (m_sPivotY + dY) * 32 + dy - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + dx + (rand() % 20 - 10), (m_sPivotY + dY) * 32 + dy - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
								}
								//Snoopy: Angels						
								if (((m_pData[dX][dY].m_status.iAngelPercent) > rand() % 6) // Angel stars
									&& (m_pData[dX][dY].m_status.HasAngelType())
									&& (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 15 + 10), (m_sPivotY + dY) * 32 - (rand() % 30) - 50, 0, 0, -(rand() % 8), 0);
								}
							}
							else if (m_pData[dX][dY].m_animation.cCurrentFrame == 4) // vu comme case 4
							{
								if ((m_pGame->m_cWhetherEffectType >= 1) && (m_pGame->m_cWhetherEffectType <= 3))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT_RAIN, (m_sPivotX + dX) * 32 + ((rand() % 20) - 10), (m_sPivotY + dY) * 32 + ((rand() % 20) - 10), 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT_RAIN, (m_sPivotX + dX) * 32 + ((rand() % 20) - 10), (m_sPivotY + dY) * 32 + ((rand() % 20) - 10), 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT_RAIN, (m_sPivotX + dX) * 32 + ((rand() % 20) - 10), (m_sPivotY + dY) * 32 + ((rand() % 20) - 10), 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT_RAIN, (m_sPivotX + dX) * 32 + ((rand() % 20) - 10), (m_sPivotY + dY) * 32 + ((rand() % 20) - 10), 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT_RAIN, (m_sPivotX + dX) * 32 + ((rand() % 20) - 10), (m_sPivotY + dY) * 32 + ((rand() % 20) - 10), 0, 0, 0, 0);
								}
								else
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT, (m_sPivotX + dX) * 32 + ((rand() % 20) - 10), (m_sPivotY + dY) * 32 + ((rand() % 20) - 10), 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT, (m_sPivotX + dX) * 32 + ((rand() % 20) - 10), (m_sPivotY + dY) * 32 + ((rand() % 20) - 10), 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT, (m_sPivotX + dX) * 32 + ((rand() % 20) - 10), (m_sPivotY + dY) * 32 + ((rand() % 20) - 10), 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT, (m_sPivotX + dX) * 32 + ((rand() % 20) - 10), (m_sPivotY + dY) * 32 + ((rand() % 20) - 10), 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT, (m_sPivotX + dX) * 32 + ((rand() % 20) - 10), (m_sPivotY + dY) * 32 + ((rand() % 20) - 10), 0, 0, 0, 0);
								}
								if (true) m_pGame->PlaySound('C', 11, sDist, lPan);
							}
							else if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) // vu comme case 5
							{
								sWeaponType = m_pData[dX][dY].m_appearance.iWeaponType;
								if ((sWeaponType >= 1) && (sWeaponType <= 2))
								{
									m_pGame->PlaySound('C', 1, sDist, lPan);
								}
								else if ((sWeaponType >= 3) && (sWeaponType <= 19))
								{
									m_pGame->PlaySound('C', 2, sDist, lPan);
								}
								else if ((sWeaponType >= 20) && (sWeaponType <= 39))
								{
									m_pGame->PlaySound('C', 18, sDist, lPan);
								}
								else if ((sWeaponType >= 40) && (sWeaponType <= 59))
								{
									m_pGame->PlaySound('C', 3, sDist, lPan);
								}
							}
							break;
						}
					}

					if ((m_pData[dX][dY].m_animation.cAction == DEF_OBJECTATTACK)) { //3 00498685
						switch (m_pData[dX][dY].m_sOwnerType) {
						case hb::owner::IceGolem: // IceGolem
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
							{
								m_pGame->m_pEffectManager->AddEffect(EffectType::AURA_EFFECT_1, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0, 0);
							}
							break;
						case hb::owner::CT: // void CGame::DrawDruncncity();Crossbow Turret (Heldenian)
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
							{
								m_pGame->m_pEffectManager->AddEffect(EffectType::GATE_ROUND, m_sPivotX + m_pData[dX][dY].m_sV1, m_sPivotY + m_pData[dX][dY].m_sV2
									, m_sPivotX + m_pData[dX][dY].m_sV1 + dX, m_sPivotY + m_pData[dX][dY].m_sV2 + dY, 0, 87);
								//m_pGame->PlaySound('E', 43, sDist, lPan); // Son "wouufffff"
							}
							break;
						case hb::owner::AGC: // void CGame::DrawDruncncity();AGT (Heldenian)
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
							{
								m_pGame->m_pEffectManager->AddEffect(EffectType::ARROW_FLYING, m_sPivotX + m_pData[dX][dY].m_sV1, m_sPivotY + m_pData[dX][dY].m_sV2
									, m_sPivotX + m_pData[dX][dY].m_sV1 + dX, m_sPivotY + m_pData[dX][dY].m_sV2 + dY, 0, 89);
								//m_pGame->PlaySound('E', 43, sDist, lPan); // Son "wouufffff"
							}
							break;
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
						case 6: // Humans
							if ((m_pData[dX][dY].m_sV3 >= 20) && (m_pData[dX][dY].m_animation.cCurrentFrame == 2))
							{
								if (m_pGame->bHasHeroSet(m_pData[dX][dY].m_appearance, m_pData[dX][dY].m_sOwnerType) == 1) // Warr hero set
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::WAR_HERO_SET, m_sPivotX + dX, m_sPivotY + dY
										, m_sPivotX + dX, m_sPivotY + dY, 0, 1);
								}
								switch (m_pData[dX][dY].m_sOwnerType) {	// Son pour critiques
								case 1:
								case 2:
								case 3:
									if (true) m_pGame->PlaySound('C', 23, sDist, lPan); // Critical sound
									break;
								case 4:
								case 5:
								case 6:
									if (true) m_pGame->PlaySound('C', 24, sDist, lPan); // Critical sound
									break;
								}
							}
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
							{
								if (m_pData[dX][dY].m_appearance.iIsWalking != 0) // not Peace mode
								{
									if (m_pData[dX][dY].m_sV3 != 1) // autre que corp � corp
									{
										m_pGame->m_pEffectManager->AddEffect(static_cast<EffectType>(m_pData[dX][dY].m_sV3), m_sPivotX + dX, m_sPivotY + dY
											, m_sPivotX + dX + m_pData[dX][dY].m_sV1, m_sPivotY + dY + m_pData[dX][dY].m_sV2
											, 0, m_pData[dX][dY].m_sOwnerType);
										if (m_pData[dX][dY].m_sV3 >= 20) m_pGame->PlaySound('E', 43, sDist, lPan); // Son "loup�"
									}
									if (m_pData[dX][dY].m_appearance.iWeaponType == 15) // StormBlade
									{
										m_pGame->m_pEffectManager->AddEffect(EffectType::STORM_BLADE, m_sPivotX + dX, m_sPivotY + dY
											, m_sPivotX + dX + m_pData[dX][dY].m_sV1, m_sPivotY + dY + m_pData[dX][dY].m_sV2
											, 0, m_pData[dX][dY].m_sOwnerType);
									}
									else
									{
										m_pGame->m_pEffectManager->AddEffect(EffectType::GATE_ROUND, m_sPivotX + dX, m_sPivotY + dY
											, m_sPivotX + dX + m_pData[dX][dY].m_sV1, m_sPivotY + dY + m_pData[dX][dY].m_sV2
											, 0, m_pData[dX][dY].m_sOwnerType);
									}
								}
								// Weapon Glare from appearance
								if ((((m_pData[dX][dY].m_appearance.iWeaponGlare | m_pData[dX][dY].m_appearance.iShieldGlare) != 0) || (m_pData[dX][dY].m_status.bGMMode)) && (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 20 - 10), (m_sPivotY + dY) * 32 - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 20 - 10), (m_sPivotY + dY) * 32 - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
								}
								//Snoopy: Angels
								if (((m_pData[dX][dY].m_status.iAngelPercent) > rand() % 6) // Angel stars
									&& (m_pData[dX][dY].m_status.HasAngelType())
									&& (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 15 + 10), (m_sPivotY + dY) * 32 - (rand() % 30) - 50, 0, 0, -(rand() % 8), 0);
								}
							}
							break;

						default:
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
							{
								if (m_pData[dX][dY].m_sV3 == 2) // Arrow flying...
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::ARROW_FLYING, m_sPivotX + dX, m_sPivotY + dY
										, m_sPivotX + dX + m_pData[dX][dY].m_sV1
										, m_sPivotY + dY + m_pData[dX][dY].m_sV2
										, 0, m_pData[dX][dY].m_sOwnerType * 1000);
								}
							}
							break;
						}

						switch (m_pData[dX][dY].m_sOwnerType) {
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
						case 6:
							if (m_pData[dX][dY].m_appearance.iIsWalking != 0)
							{
								sWeaponType = m_pData[dX][dY].m_appearance.iWeaponType;
								if ((sWeaponType >= 1) && (sWeaponType <= 2))
								{
									if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									{
										m_pGame->PlaySound('C', 1, sDist, lPan);
									}
								}
								else if ((sWeaponType >= 3) && (sWeaponType <= 19))
								{
									if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									{
										m_pGame->PlaySound('C', 2, sDist, lPan);
									}
								}
								else if ((sWeaponType >= 20) && (sWeaponType <= 39))
								{
									if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									{
										m_pGame->PlaySound('C', 18, sDist, lPan);
									}
								}
								else if ((sWeaponType >= 40) && (sWeaponType <= 59))
								{
									if (m_pData[dX][dY].m_animation.cCurrentFrame == 3)
									{
										m_pGame->PlaySound('C', 3, sDist, lPan);
									}
								}
							}
							break;

						case hb::owner::ATK: // Snoopy: ATK
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 140, sDist, lPan);
							break;

						case hb::owner::MasterElf: // Snoopy: MasterElf
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('C', 8, sDist, lPan);
							break;

						case hb::owner::DSK: // Snoopy: DSK
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 145, sDist, lPan);
							break;

						case hb::owner::Beholder: // Beholder
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('E', 46, sDist, lPan);
							break;

						case hb::owner::DarkElf: // DE
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
							{
								if (true) m_pGame->PlaySound('C', 3, sDist, lPan);
							}
							break;

						case hb::owner::TigerWorm: // TW
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
							{
								if (true) m_pGame->PlaySound('C', 1, sDist, lPan);
							}
							break;

						case hb::owner::Slime: // Slime
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 2, sDist, lPan);
							break;

						case hb::owner::Skeleton: // Skell
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 14, sDist, lPan);
							break;

						case hb::owner::StoneGolem: // Stone-Golem
						case hb::owner::IceGolem: // ICeGolem
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 34, sDist, lPan);
							break;

						case hb::owner::Cyclops: // Cyclops
						case hb::owner::HellClaw: // HC
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 42, sDist, lPan);
							break;

						case hb::owner::GodsHandKnight: // GHK
						case hb::owner::GodsHandKnightCK: // GHKABS
						case hb::owner::TempleKnight: // TK
						case hb::owner::Gargoyle: // GG
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
							{
								if (true) m_pGame->PlaySound('C', 2, sDist, lPan);
							}
							break;

						case hb::owner::OrcMage: // orc
						case hb::owner::Stalker: // SK
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 10, sDist, lPan);
							break;

						case hb::owner::GiantAnt: // Ant
						case hb::owner::LightWarBeetle: // LWB
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 30, sDist, lPan);
							break;

						case hb::owner::Scorpion: // Scorpion
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 22, sDist, lPan);
							break;

						case hb::owner::Zombie: // Zombie
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 18, sDist, lPan);
							break;

						case hb::owner::Amphis: // Snake
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 26, sDist, lPan);
							break;

						case hb::owner::ClayGolem: // Clay-Golem
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 38, sDist, lPan);
							break;

						case hb::owner::Hellhound: // HH
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 6, sDist, lPan);
							break;

						case hb::owner::Troll: // Troll
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 47, sDist, lPan);
							break;

						case hb::owner::Ogre: // Ogre
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 52, sDist, lPan);
							break;

						case hb::owner::Liche: // Liche
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 56, sDist, lPan);
							break;

						case hb::owner::Demon: // DD
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 60, sDist, lPan);
							break;

						case hb::owner::Unicorn: // Uni
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 64, sDist, lPan);
							break;

						case hb::owner::WereWolf: // WW
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								m_pGame->PlaySound('M', 68, sDist, lPan);
							break;

						case hb::owner::Bunny://Rabbit
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 75, sDist, lPan);
							break;

						case hb::owner::Cat://Cat
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 76, sDist, lPan);
							break;

						case hb::owner::GiantFrog://Giant-Frog
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 77, sDist, lPan);
							break;

						case hb::owner::MountainGiant://Mountain Giant
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 88, sDist, lPan);
							break;

						case hb::owner::Ettin://Ettin
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 92, sDist, lPan);
							break;

						case hb::owner::CannibalPlant://Cannibal Plant
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 96, sDist, lPan);
							break;

						case hb::owner::Rudolph://Rudolph
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
							{
								if (true) m_pGame->PlaySound('M', 38, sDist, lPan);
							}
							break;

						case hb::owner::DireBoar://DireBoar
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 68, sDist, lPan);
							break;

						case hb::owner::Frost://Frost
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
							{
								if (true) m_pGame->PlaySound('C', 4, sDist, lPan);
							}
							break;

						case hb::owner::MasterOrc: // Snoopy: Master MageOrc
						case hb::owner::Barbarian: // Snoopy: Barbarian
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 78, sDist, lPan);
							break;

						case hb::owner::GiantCrayfish: // Snoopy: GiantCrayFish
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 100, sDist, lPan);
							break;

						case hb::owner::FireWyvern: // Snoopy: Fire Wyvern
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 107, sDist, lPan);
							break;

						case hb::owner::Tentocle: // Snoopy: Tentocle
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 111, sDist, lPan);
							break;

						case hb::owner::Abaddon: // Snoopy: Abaddon
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 137, sDist, lPan);
							break;

						case hb::owner::ClawTurtle: // Snoopy: Claw-Turtle
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 115, sDist, lPan);
							break;

						case hb::owner::Centaur: // Snoopy: Centaurus
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 119, sDist, lPan);
							break;

						case hb::owner::GiTree: // Snoopy: Giant-Tree
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 123, sDist, lPan);
							break;

						case hb::owner::GiLizard: // Snoopy: GiantLizard
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 127, sDist, lPan);
							break;

						case hb::owner::Dragon: // Snoopy: Dragon
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 131, sDist, lPan);
							break;

						case hb::owner::Nizie: //Snoopy:  Nizie
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 135, sDist, lPan);
							break;

						case hb::owner::Minaus: // Snoopy: Minaus
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 104, sDist, lPan);
							break;

						case hb::owner::HBT: // Snoopy: Heavy BattleTank
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 151, sDist, lPan);
							break;

						case hb::owner::CT: // Snoopy: Crosbow Turret
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 153, sDist, lPan);
							break;

						case hb::owner::AGC: // Snoopy: Cannon Turret
							if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
								m_pGame->PlaySound('M', 155, sDist, lPan);
							break;

						case hb::owner::Dummy: // Dummy
						case hb::owner::EnergySphere: // Snoopy: EnergySphere
						default:
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 2) {
								if (true) m_pGame->PlaySound('C', 2, sDist, lPan);
							}
							break;
						}
					}

					if (m_pData[dX][dY].m_animation.cAction == DEF_OBJECTDAMAGE)  // 6  00499159
					{
						switch (m_pData[dX][dY].m_sOwnerType) {
						case 1:
						case 2:
						case 3:  // Men
						case hb::owner::GodsHandKnight: // GHK
						case hb::owner::GodsHandKnightCK: // GHKABS
						case hb::owner::TempleKnight: // TK
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 4)
							{
								if (m_pData[dX][dY].m_sV2 == -1)
									iSoundIndex = 5;
								else if (m_pData[dX][dY].m_sV2 == 0)
									iSoundIndex = 5;
								else if ((m_pData[dX][dY].m_sV2 >= 1) && (m_pData[dX][dY].m_sV2 <= 19))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 20) && (m_pData[dX][dY].m_sV2 <= 39))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 40) && (m_pData[dX][dY].m_sV2 <= 59))
									iSoundIndex = 7;
								else iSoundIndex = 5;
								if (true) m_pGame->PlaySound('C', iSoundIndex, sDist, lPan);
								m_pGame->m_pEffectManager->AddEffect(EffectType::NORMAL_HIT, m_sPivotX + dX, m_sPivotY + dY, 0, 0, 0, 4);
							}
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
							{
								if (true) m_pGame->PlaySound('C', 12, sDist, lPan);
							}
							break;
						case 4:
						case 5:
						case 6: // Women
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 4)
							{
								if (m_pData[dX][dY].m_sV2 == -1)
									iSoundIndex = 5;
								else if (m_pData[dX][dY].m_sV2 == 0)
									iSoundIndex = 5;
								else if ((m_pData[dX][dY].m_sV2 >= 1) && (m_pData[dX][dY].m_sV2 <= 19))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 20) && (m_pData[dX][dY].m_sV2 <= 39))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 40) && (m_pData[dX][dY].m_sV2 <= 59))
									iSoundIndex = 7;
								else iSoundIndex = 5;
								if (true) m_pGame->PlaySound('C', iSoundIndex, sDist, lPan);
								m_pGame->m_pEffectManager->AddEffect(EffectType::NORMAL_HIT, m_sPivotX + dX, m_sPivotY + dY, 0, 0, 0, 4);
							}
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
							{
								if (true) m_pGame->PlaySound('C', 13, sDist, lPan);
							}
							break;

						default:
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 4)
							{
								if (m_pData[dX][dY].m_sV2 == -1)
									iSoundIndex = 5;  // Hand Attack
								else if (m_pData[dX][dY].m_sV2 == 0)
									iSoundIndex = 5;  // Hand Attack
								else if ((m_pData[dX][dY].m_sV2 >= 1) && (m_pData[dX][dY].m_sV2 <= 19))
									iSoundIndex = 6;  // Blade hit
								else if ((m_pData[dX][dY].m_sV2 >= 20) && (m_pData[dX][dY].m_sV2 <= 39))
									iSoundIndex = 6;  // Blade hit
								else if ((m_pData[dX][dY].m_sV2 >= 40) && (m_pData[dX][dY].m_sV2 <= 59))
									iSoundIndex = 7; // Arrow hit
								else iSoundIndex = 5;

								if (true) m_pGame->PlaySound('C', iSoundIndex, sDist, lPan);
								if (iSoundIndex == 7) // Change the effect for Arrows hitting (no more at fixed heigh with arrow flying but on damage)
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0, m_pData[dX][dY].m_sOwnerType);
								}
								else
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::NORMAL_HIT, m_sPivotX + dX, m_sPivotY + dY, 0, 0, 0, 4);
								}
							}

							switch (m_pData[dX][dY].m_sOwnerType) {
							case hb::owner::Barbarian: // Snoopy: Barbarian
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1 && true) m_pGame->PlaySound('M', 144, sDist, lPan);
								break;

							case hb::owner::ATK: // Snoopy: ATK
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1 && true) m_pGame->PlaySound('M', 143, sDist, lPan);
								break;

							case hb::owner::MasterElf: // Snoopy: MasterElf
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1) m_pGame->PlaySound('C', 7, sDist, lPan);
								break;

							case hb::owner::DSK: // Snoopy: DSK
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1) m_pGame->PlaySound('M', 148, sDist, lPan);
								break;

							case hb::owner::DarkElf: // DE
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5 && true) m_pGame->PlaySound('C', 13, sDist, lPan);
								break;

							case hb::owner::Slime: // Slime
							case hb::owner::Beholder: // BB
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 3, sDist, lPan);
								break;

							case hb::owner::Skeleton: // Skell
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 15, sDist, lPan);
								break;

							case hb::owner::StoneGolem: // Stone-Golem
							case hb::owner::IceGolem: // IceGolem
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 35, sDist, lPan);
								break;

							case hb::owner::Cyclops: // Cyclops
							case hb::owner::HellClaw: // HC
							case hb::owner::Gargoyle: // GG
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 43, sDist, lPan);
								break;

							case hb::owner::OrcMage: // Orc
							case hb::owner::Stalker: // SK
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 11, sDist, lPan);
								break;

							case hb::owner::GiantAnt: // Ant
							case hb::owner::LightWarBeetle: // LWB
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 31, sDist, lPan);
								break;

							case hb::owner::Scorpion: // Scorp
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 23, sDist, lPan);
								break;

							case hb::owner::Zombie: // Zombie
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 19, sDist, lPan);
								break;

							case hb::owner::Amphis: // Snake
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 27, sDist, lPan);
								break;

							case hb::owner::ClayGolem: // Clay-Golem
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 39, sDist, lPan);
								break;

							case hb::owner::Hellhound: // HH
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 7, sDist, lPan);
								break;

							case hb::owner::Troll: // Troll
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 48, sDist, lPan);
								break;

							case hb::owner::Ogre: // Ogre
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 53, sDist, lPan);
								break;

							case hb::owner::Liche: // Liche
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 57, sDist, lPan);
								break;

							case hb::owner::Demon: // DD
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 61, sDist, lPan);
								break;

							case hb::owner::Unicorn: // Uni
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 65, sDist, lPan);
								break;

							case hb::owner::WereWolf: // WW
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 69, sDist, lPan);
								break;

							case hb::owner::Dummy: // dummy
							case hb::owner::EnergySphere: // Snoopy: EnergyBall
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) m_pGame->PlaySound('M', 2, sDist, lPan);
								break;

							case hb::owner::Bunny://Rabbit
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 79, sDist, lPan);
								break;

							case hb::owner::Cat://Cat
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 80, sDist, lPan);
								break;

							case hb::owner::GiantFrog://Giant-Frog
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 81, sDist, lPan);
								break;

							case hb::owner::MountainGiant: // Mountain Giant
							case hb::owner::MasterOrc: // Snoopy: MasterMageOrc
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 89, sDist, lPan);
								break;

							case hb::owner::Ettin://Ettin
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 93, sDist, lPan);
								break;
							case hb::owner::CannibalPlant://Cannabl Plant
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 97, sDist, lPan);
								break;
							case hb::owner::Rudolph://Rudolph
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 69, sDist, lPan);
								break;
							case hb::owner::DireBoar://DireBoar
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 78, sDist, lPan);
								break;
							case hb::owner::Frost://Frost
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1) m_pGame->PlaySound('C', 13, sDist, lPan);
								break;

							case hb::owner::GiantCrayfish: // Snoopy: Giant CrayFish
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 101, sDist, lPan);
								break;

							case hb::owner::Minaus: // Snoopy: Minaus
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 102, sDist, lPan);
								break;

							case hb::owner::Tentocle: // Snoopy: Tentocle
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 108, sDist, lPan);
								break;

							case hb::owner::Abaddon: // Snoopy: Abaddon
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 138, sDist, lPan);
								break;

							case hb::owner::ClawTurtle: // Snoopy: ClawTurtle
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 112, sDist, lPan);
								break;

							case hb::owner::Centaur: // Snoopy: Centaurus
							case hb::owner::Sorceress: // Snoopy: Sorceress
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 116, sDist, lPan);
								break;

							case hb::owner::GiTree: // Snoopy: GiantTree
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 120, sDist, lPan);
								break;

							case hb::owner::GiLizard: // Snoopy: GiantLizard
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 124, sDist, lPan);
								break;

							case hb::owner::Dragon: // Snoopy: Dragon
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 128, sDist, lPan);
								break;

							case hb::owner::Nizie: // Snoopy: Nizie
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1)) m_pGame->PlaySound('M', 132, sDist, lPan);
								break;
							}
							break;
						}
					}

					if (m_pData[dX][dY].m_animation.cAction == DEF_OBJECTDAMAGEMOVE) { // 7 004997BD
						switch (m_pData[dX][dY].m_sOwnerType) {
						case 1:
						case 2:
						case 3:
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
							{
								if (m_pData[dX][dY].m_sV2 == -1)
									iSoundIndex = 5;
								else if (m_pData[dX][dY].m_sV2 == 0)
									iSoundIndex = 5;
								else if ((m_pData[dX][dY].m_sV2 >= 1) && (m_pData[dX][dY].m_sV2 <= 19))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 20) && (m_pData[dX][dY].m_sV2 <= 39))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 40) && (m_pData[dX][dY].m_sV2 <= 59))
									iSoundIndex = 7;
								else iSoundIndex = 5;

								if (true) m_pGame->PlaySound('C', iSoundIndex, sDist, lPan);
								m_pGame->m_pEffectManager->AddEffect(EffectType::NORMAL_HIT, m_sPivotX + dX, m_sPivotY + dY, 0, 0, 0, 4);
							}
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
							{
								if (true) m_pGame->PlaySound('C', 12, sDist, lPan);
							}
							break;

						case 4:
						case 5:
						case 6:
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
							{
								if (m_pData[dX][dY].m_sV2 == -1)
									iSoundIndex = 5;
								else if (m_pData[dX][dY].m_sV2 == 0)
									iSoundIndex = 5;
								else if ((m_pData[dX][dY].m_sV2 >= 1) && (m_pData[dX][dY].m_sV2 <= 19))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 20) && (m_pData[dX][dY].m_sV2 <= 39))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 40) && (m_pData[dX][dY].m_sV2 <= 59))
									iSoundIndex = 7;
								else iSoundIndex = 5;
								if (true) m_pGame->PlaySound('C', iSoundIndex, sDist, lPan);
								m_pGame->m_pEffectManager->AddEffect(EffectType::NORMAL_HIT, m_sPivotX + dX, m_sPivotY + dY, 0, 0, 0, 4);
							}
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
							{
								if (true) m_pGame->PlaySound('C', 13, sDist, lPan);
							}
							break;

						default:
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
							{
								if (m_pData[dX][dY].m_sV2 == -1)
									iSoundIndex = 5;
								else if (m_pData[dX][dY].m_sV2 == 0)
									iSoundIndex = 5;
								else if ((m_pData[dX][dY].m_sV2 >= 1) && (m_pData[dX][dY].m_sV2 <= 19))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 20) && (m_pData[dX][dY].m_sV2 <= 39))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 40) && (m_pData[dX][dY].m_sV2 <= 59))
									iSoundIndex = 7;
								else iSoundIndex = 5;
								if (true) m_pGame->PlaySound('C', iSoundIndex, sDist, lPan);
								m_pGame->m_pEffectManager->AddEffect(EffectType::NORMAL_HIT, m_sPivotX + dX, m_sPivotY + dY, 0, 0, 0, 4);
							}

							switch (m_pData[dX][dY].m_sOwnerType) {
							case hb::owner::ATK: //Snoopy:  ATK
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
									m_pGame->PlaySound('M', 143, sDist, lPan);
								break;
							case hb::owner::MasterElf: // Snoopy: MasterElf
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
									m_pGame->PlaySound('C', 7, sDist, lPan);
								break;
							case hb::owner::Barbarian: // Snoopy: Barbarian
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
									m_pGame->PlaySound('M', 144, sDist, lPan);
								break;
							case hb::owner::DSK: // Snoopy: DSK
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
									m_pGame->PlaySound('M', 148, sDist, lPan);
								break;

							case hb::owner::Slime: // Slime
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 3, sDist, lPan);
								break;

							case hb::owner::Skeleton: // Skell
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 15, sDist, lPan);
								break;

							case hb::owner::StoneGolem: // Stone Golem
							case hb::owner::IceGolem: // IceGolem
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 35, sDist, lPan);
								break;

							case hb::owner::Cyclops: // Cyclops
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 43, sDist, lPan);
								break;

							case hb::owner::OrcMage: // Orc
							case hb::owner::Stalker: // SK
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 11, sDist, lPan);
								break;

							case hb::owner::GiantAnt: // Ant
							case hb::owner::LightWarBeetle: // LWB
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 31, sDist, lPan);
								break;

							case hb::owner::Scorpion: // Scorpion
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 23, sDist, lPan);
								break;

							case hb::owner::Zombie: // Zombie
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 19, sDist, lPan);
								break;

							case hb::owner::Amphis: // Snake
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 27, sDist, lPan);
								break;

							case hb::owner::ClayGolem: // Clay-Golem
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 39, sDist, lPan);
								break;

							case hb::owner::Hellhound: // HH
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 7, sDist, lPan);
								break;

							case hb::owner::Troll: // Troll
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 48, sDist, lPan);
								break;

							case hb::owner::Ogre: // Ogre
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 53, sDist, lPan);
								break;

							case hb::owner::Liche: // Liche
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 57, sDist, lPan);
								break;

							case hb::owner::Demon: // DD
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 61, sDist, lPan);
								break;

							case hb::owner::Unicorn: // Uni
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 65, sDist, lPan);
								break;

							case hb::owner::WereWolf: // WW
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 2)
									m_pGame->PlaySound('M', 69, sDist, lPan);
								break;
							case hb::owner::Bunny://Rabbit
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 79, sDist, lPan);
								break;

							case hb::owner::Cat://Cat
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 80, sDist, lPan);
								break;

							case hb::owner::GiantFrog://Giant-Frog
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 81, sDist, lPan);
								break;

							case hb::owner::MountainGiant://Mountain Giant
							case hb::owner::MasterOrc: // Snoopy: MasterMageOrc
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 89, sDist, lPan);
								break;

							case hb::owner::Ettin://Ettin
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 93, sDist, lPan);
								break;

							case hb::owner::CannibalPlant://Cannibal Plant
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 97, sDist, lPan);
								break;

							case hb::owner::Rudolph://Rudolph
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 69, sDist, lPan);
								break;
							case hb::owner::DireBoar://DireBoar
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 78, sDist, lPan);
								break;

							case hb::owner::GiantCrayfish: //Snoopy:  GiantCrayFish
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 101, sDist, lPan);
								break;

							case hb::owner::Minaus: // Snoopy: Minos
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 101, sDist, lPan);
								break;

							case hb::owner::Tentocle: // Snoopy: Tentocle
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 108, sDist, lPan);
								break;

							case hb::owner::Abaddon: // Snoopy: Abaddon
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 138, sDist, lPan);
								break;

							case hb::owner::ClawTurtle: // Snoopy: ClawTurtle
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 112, sDist, lPan);
								break;

							case hb::owner::Centaur: // Snoopy: Centaurus
							case hb::owner::Sorceress: // Snoopy: Sorceress
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 116, sDist, lPan);
								break;

							case hb::owner::GiTree: // Snoopy: GiantTree
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 120, sDist, lPan);
								break;

							case hb::owner::GiLizard: // Snoopy: GiantLizard
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 124, sDist, lPan);
								break;

							case hb::owner::Dragon: // Snoopy: Dragon
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 128, sDist, lPan);
								break;

							case hb::owner::Nizie: // Snoopy: Nizie
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 132, sDist, lPan);
								break;

							default:
								break;
							}
							break;
						}
					}

					if (m_pData[dX][dY].m_animation.cAction == DEF_OBJECTMAGIC)  // 4 00499D51
					{
						switch (m_pData[dX][dY].m_sOwnerType) {
						case 1:
						case 2:
						case 3:
						case 4:
						case 5:
						case 6:
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
							{
								if (true) m_pGame->PlaySound('C', 16, sDist, lPan);
								if ((((m_pData[dX][dY].m_appearance.iWeaponGlare | m_pData[dX][dY].m_appearance.iShieldGlare) != 0) || (m_pData[dX][dY].m_status.bGMMode)) && (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 20 - 10), (m_sPivotY + dY) * 32 - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 20 - 10), (m_sPivotY + dY) * 32 - (rand() % 50) - 5, 0, 0, -(rand() % 8), 0);
								}
								//Snoopy: Angels
								if (((m_pData[dX][dY].m_status.iAngelPercent) > rand() % 6) // Angel stars
									&& (m_pData[dX][dY].m_status.HasAngelType())
									&& (!m_pData[dX][dY].m_status.bInvisibility))
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::STAR_TWINKLE, (m_sPivotX + dX) * 32 + (rand() % 15 + 10), (m_sPivotY + dY) * 32 - (rand() % 30) - 50, 0, 0, -(rand() % 8), 0);
								}
								if (m_pGame->bHasHeroSet(m_pData[dX][dY].m_appearance, m_pData[dX][dY].m_sOwnerType) == 2) // Mage hero set
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::MAGE_HERO_SET, m_sPivotX + dX, m_sPivotY + dY
										, m_sPivotX + dX, m_sPivotY + dY, 0, 1);
								}
								if (m_pData[dX][dY].m_sV1 >= 70) // effet gros sorts autour du caster
									m_pGame->m_pEffectManager->AddEffect(EffectType::BUFF_EFFECT_LIGHT, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0, 0);
								if (m_pData[dX][dY].m_sV1 == 82) // lumi�re si MassMagicMissile autour du caster
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::MASS_MM_AURA_CASTER, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0, 0);
								}
							}
							break;
						}
					}

					if (m_pData[dX][dY].m_animation.cAction == DEF_OBJECTDYING)  // 10 // 00499F5D
					{
						switch (m_pData[dX][dY].m_sOwnerType) {
						case 1:
						case 2:
						case 3:
						case hb::owner::GodsHandKnight: // GHK
						case hb::owner::GodsHandKnightCK: // GHKABS
						case hb::owner::TempleKnight: // TK
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 6)
							{
								if (m_pData[dX][dY].m_sV2 == -1)
									iSoundIndex = 5;
								else if (m_pData[dX][dY].m_sV2 == 0)
									iSoundIndex = 5;
								else if ((m_pData[dX][dY].m_sV2 >= 1) && (m_pData[dX][dY].m_sV2 <= 19))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 20) && (m_pData[dX][dY].m_sV2 <= 39))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 40) && (m_pData[dX][dY].m_sV2 <= 59))
									iSoundIndex = 7;
								else iSoundIndex = 5;
								if (true) m_pGame->PlaySound('C', iSoundIndex, sDist, lPan);
								m_pGame->m_pEffectManager->AddEffect(EffectType::NORMAL_HIT, m_sPivotX + dX, m_sPivotY + dY, 0, 0, 0, 12);
							}
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 7)
							{
								if (true) m_pGame->PlaySound('C', 14, sDist, lPan);
							}
							break;

						case 4:
						case 5:
						case 6:
						case hb::owner::DarkElf: // DE
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 6)
							{
								if (m_pData[dX][dY].m_sV2 == -1)
									iSoundIndex = 5;
								else if (m_pData[dX][dY].m_sV2 == 0)
									iSoundIndex = 5;
								else if ((m_pData[dX][dY].m_sV2 >= 1) && (m_pData[dX][dY].m_sV2 <= 19))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 20) && (m_pData[dX][dY].m_sV2 <= 39))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 40) && (m_pData[dX][dY].m_sV2 <= 59))
									iSoundIndex = 7;
								else iSoundIndex = 5;
								if (true) m_pGame->PlaySound('C', iSoundIndex, sDist, lPan);
								m_pGame->m_pEffectManager->AddEffect(EffectType::NORMAL_HIT, m_sPivotX + dX, m_sPivotY + dY, 0, 0, 0, 12);
							}
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 7)
							{
								if (true) m_pGame->PlaySound('C', 15, sDist, lPan);
							}
							break;

						default:
							if (m_pData[dX][dY].m_animation.cCurrentFrame == 4)
							{
								if (m_pData[dX][dY].m_sV2 == -1)
									iSoundIndex = 5;
								else if (m_pData[dX][dY].m_sV2 == 0)
									iSoundIndex = 5;
								else if ((m_pData[dX][dY].m_sV2 >= 1) && (m_pData[dX][dY].m_sV2 <= 19))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 20) && (m_pData[dX][dY].m_sV2 <= 39))
									iSoundIndex = 6;
								else if ((m_pData[dX][dY].m_sV2 >= 40) && (m_pData[dX][dY].m_sV2 <= 59))
									iSoundIndex = 7;
								else iSoundIndex = 5;
								if (true) m_pGame->PlaySound('C', iSoundIndex, sDist, lPan);
								m_pGame->m_pEffectManager->AddEffect(EffectType::NORMAL_HIT, m_sPivotX + dX, m_sPivotY + dY, 0, 0, 0, 12);
							}

							switch (m_pData[dX][dY].m_sOwnerType) {
							case hb::owner::Beholder: // BB
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 39, sDist, lPan);
								break;

							case hb::owner::Slime: // Slime
							case hb::owner::Dummy: // Dummy
							case hb::owner::EnergySphere: // Snoopy: EnergyBall
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 4, sDist, lPan);
								break;

							case hb::owner::Skeleton: // Skell
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 16, sDist, lPan);
								break;

							case hb::owner::StoneGolem: // Stone-Golem
							case hb::owner::BattleGolem: // BG
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 36, sDist, lPan);
								break;

							case hb::owner::IceGolem: // IceGolem
								//							if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								//								m_pGame->m_pEffectManager->AddEffect(EffectType::AURA_EFFECT_2, (m_sPivotX+dX)*32, (m_sPivotY+dY)*32, 0, 0, 0 );
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5) {
									m_pGame->m_pEffectManager->AddEffect(EffectType::AURA_EFFECT_2, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0);
									m_pGame->PlaySound('M', 36, sDist, lPan);
								}
								break;

							case hb::owner::Cyclops: // Cyclops
							case hb::owner::HellClaw: // HC
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 44, sDist, lPan);
								break;

							case hb::owner::OrcMage: // Orc
							case hb::owner::Stalker: // SK
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 12, sDist, lPan);
								break;

							case hb::owner::GiantAnt: // Ant
							case hb::owner::LightWarBeetle: // LWB
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 32, sDist, lPan);
								break;

							case hb::owner::Scorpion: // Scorp
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 24, sDist, lPan);
								break;

							case hb::owner::Zombie: // Zombie
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 20, sDist, lPan);
								break;

							case hb::owner::Amphis: // Snake
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 28, sDist, lPan);
								break;

							case hb::owner::ClayGolem: // Clay-Golem
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 40, sDist, lPan);
								break;

							case hb::owner::Hellhound: // HH
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 8, sDist, lPan);
								break;

							case hb::owner::Troll: // Troll
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 49, sDist, lPan);
								break;

							case hb::owner::Ogre: // Ogre
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 54, sDist, lPan);
								break;

							case hb::owner::Liche: // Liche
							case hb::owner::TigerWorm: // TW
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 58, sDist, lPan);
								break;

							case hb::owner::Demon: // DD
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 62, sDist, lPan);
								break;

							case hb::owner::Unicorn: // Uni
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 66, sDist, lPan);
								break;

							case hb::owner::WereWolf: // WW
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('M', 70, sDist, lPan);
								break;

							case hb::owner::ArrowGuardTower: // AGT
							case hb::owner::CannonGuardTower: // CGT
							case hb::owner::ManaCollector: // MS
							case hb::owner::Detector: // DT
							case hb::owner::EnergyShield: // ESG
							case hb::owner::GrandMagicGenerator: // GMG
							case hb::owner::ManaStone: // ManaStone
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 3)
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_EXPLOSION, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60), 0, 0, -1 * (rand() % 2));
								}
								break;

							case hb::owner::CT: // Snoopy: CrossBowTurret
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 3)
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_EXPLOSION, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60), 0, 0, -1 * (rand() % 2));
								}
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
									m_pGame->PlaySound('M', 154, sDist, lPan);
								break;

							case hb::owner::AGC: // Snoopy: CannonTurret
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 3)
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_EXPLOSION, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60), 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60), 0, 0, -1 * (rand() % 2));
								}
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
									m_pGame->PlaySound('M', 156, sDist, lPan);
								break;

							case hb::owner::Catapult: // CP
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 1)
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_EXPLOSION, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32 - 30, 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60) - 30, 0, 0, -1 * (rand() % 2));
								}
								break;

							case hb::owner::Gargoyle: // GG
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
								{
									m_pGame->PlaySound('M', 44, sDist, lPan);
								}
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 11)
								{
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_EXPLOSION, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32 - 30, 0, 0, 0, 0);
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::BURST_LARGE, (m_sPivotX + dX) * 32 + 5 - (rand() % 10), (m_sPivotY + dY) * 32 + 5 - (rand() % 10) - 30, 0, 0, -1 * (rand() % 2));

									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60) - 30, 0, 0, -1 * (rand() % 2));
									m_pGame->m_pEffectManager->AddEffect(EffectType::MS_CRUSADE_CASTING, (m_sPivotX + dX) * 32 + 30 - (rand() % 60), (m_sPivotY + dY) * 32 + 30 - (rand() % 60) - 30, 0, 0, -1 * (rand() % 2));
								}
								break;

							case hb::owner::Bunny:// Rabbit
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 83, sDist, lPan);
								break;

							case hb::owner::Cat: // Cat
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 84, sDist, lPan);
								break;

							case hb::owner::GiantFrog://Giant-Frog
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 85, sDist, lPan);
								break;

							case hb::owner::MountainGiant://Mountain Giant
							case hb::owner::MasterOrc: // Snoopy: MasterMageOrc
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 90, sDist, lPan);
								break;

							case hb::owner::Ettin://Ettin
							case hb::owner::Barbarian: // Snoopy: Barbarian
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 94, sDist, lPan);
								break;

							case hb::owner::ATK: // Snoopy: ATK
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 141, sDist, lPan);
								break;

							case hb::owner::DSK: // Snoopy: DSK
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 146, sDist, lPan);
								break;

							case hb::owner::Rudolph://Rudolph
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 65, sDist, lPan);
								break;

							case hb::owner::DireBoar://DireBoar
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 94, sDist, lPan);
								break;

							case hb::owner::Wyvern: // Wyvern
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('E', 7, sDist, lPan);
								break;

							case hb::owner::Dragon: // Snoopy: Dragon
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 129, sDist, lPan);
								break;

							case hb::owner::Centaur: // Snoopy: Centaur
							case hb::owner::Sorceress: // Snoopy: Sorceress
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 129, sDist, lPan);
								break;

							case hb::owner::ClawTurtle: // Snoopy: ClawTurtle
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 113, sDist, lPan);
								break;

							case hb::owner::FireWyvern: // Snoopy: FireWyvern
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 105, sDist, lPan);
								break;


							case hb::owner::CannibalPlant: // Cannibal Plant
							case hb::owner::GiantCrayfish: // Snoopy: GiantGrayFish
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 98, sDist, lPan);
								break;

							case hb::owner::GiLizard: //Snoopy:
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 125, sDist, lPan);
								break;

							case hb::owner::GiTree: // Snoopy:
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 121, sDist, lPan);
								break;

							case hb::owner::Minaus: // Snoopy:
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 103, sDist, lPan);
								break;

							case hb::owner::Nizie: // Snoopy:
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 133, sDist, lPan);
								break;

							case hb::owner::Tentocle: //Snoopy: Tentocle
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 109, sDist, lPan);
								break;

							case hb::owner::Abaddon: // Snoopy: Abaddon
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 139, sDist, lPan);
								break;

							case hb::owner::MasterElf: // Snoopy: MasterElf
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 150, sDist, lPan);
								break;

							case hb::owner::HBT: // Snoopy: HBT
								if ((m_pData[dX][dY].m_animation.cCurrentFrame == 1))
									m_pGame->PlaySound('M', 152, sDist, lPan);
								break;

							default:
								if (m_pData[dX][dY].m_animation.cCurrentFrame == 5)
									m_pGame->PlaySound('C', 15, sDist, lPan);
								break;

							case hb::owner::Frost: // Frost
							case hb::owner::Gate: // Snoopy: Gate
								break;
							}
							break;
						}
					}
				}
			}
		}
	if (bAutoUpdate)
	{
		S_dwUpdateTime = dwTime;
		if (iRet == 0)
			return -1;
	}
	if (dynObjsNeedUpdate) m_dwDOframeTime = dwTime; //v1.4
	return iRet;
}


bool CMapData::bSetItem(short sX, short sY, short sIDnum, char cItemColor, uint32_t dwItemAttr, bool bDropEffect)
{
	int dX, dY;
	int sAbsX, sAbsY, sDist;
	if ((sX < m_sPivotX) || (sX >= m_sPivotX + MAPDATASIZEX) ||
		(sY < m_sPivotY) || (sY >= m_sPivotY + MAPDATASIZEY))
	{
		return false;
	}

	dX = sX - m_sPivotX;
	dY = sY - m_sPivotY;

	m_pData[dX][dY].m_sItemID = sIDnum;
	m_pData[dX][dY].m_dwItemAttr = dwItemAttr;
	m_pData[dX][dY].m_cItemColor = cItemColor;

	sAbsX = abs(((m_pGame->m_Camera.GetX() / 32) + VIEW_CENTER_TILE_X()) - sX);
	sAbsY = abs(((m_pGame->m_Camera.GetY() / 32) + VIEW_CENTER_TILE_Y()) - sY);

	if (sAbsX > sAbsY) sDist = sAbsX;
	else sDist = sAbsY;

	if (sIDnum != 0)
	{
		if (bDropEffect == true)
		{
			m_pGame->PlaySound('E', 11, sDist);
			m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0, 0);
			m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT, (m_sPivotX + dX) * 32 + (10 - (rand() % 20)), (m_sPivotY + dY) * 32 + (10 - (rand() % 20)), 0, 0, (rand() % 2), 0);
			m_pGame->m_pEffectManager->AddEffect(EffectType::FOOTPRINT, (m_sPivotX + dX) * 32 + (10 - (rand() % 20)), (m_sPivotY + dY) * 32 + (10 - (rand() % 20)), 0, 0, (rand() % 2), 0);
		}
	}

	return true;
}

bool __fastcall CMapData::bSetDeadOwner(uint16_t wObjectID, short sX, short sY, short sType, char cDir, const PlayerAppearance& appearance, const PlayerStatus& status, char* pName)
{
	int  dX, dY;
	char pTmpName[12];
	bool bEraseFlag = false;

	std::memset(pTmpName, 0, sizeof(pTmpName));
	if (pName != 0) strcpy(pTmpName, pName);
	if ((sX < m_sPivotX) || (sX >= m_sPivotX + MAPDATASIZEX) ||
		(sY < m_sPivotY) || (sY >= m_sPivotY + MAPDATASIZEY))
	{
		for (dX = 0; dX < MAPDATASIZEX; dX++)
			for (dY = 0; dY < MAPDATASIZEY; dY++)
			{
				if (memcmp(m_pData[dX][dY].m_cDeadOwnerName, pTmpName, 10) == 0)
				{
					m_pData[dX][dY].m_sDeadOwnerType = 0;
					std::memset(m_pData[dX][dY].m_cDeadOwnerName, 0, sizeof(m_pData[dX][dY].m_cDeadOwnerName));
				}
			}
		return false;
	}

	for (dX = sX - 2; dX <= sX + 2; dX++)
		for (dY = sY - 2; dY <= sY + 2; dY++)
		{
			if (dX < m_sPivotX) break;
			else
				if (dX > m_sPivotX + MAPDATASIZEX) break;
			if (dY < m_sPivotY) break;
			else
				if (dY > m_sPivotY + MAPDATASIZEY) break;

			if (memcmp(m_pData[dX - m_sPivotX][dY - m_sPivotY].m_cDeadOwnerName, pTmpName, 10) == 0)
			{
				m_pData[dX - m_sPivotX][dY - m_sPivotY].m_sDeadOwnerType = 0;
				std::memset(m_pData[dX - m_sPivotX][dY - m_sPivotY].m_cDeadOwnerName, 0, sizeof(m_pData[dX - m_sPivotX][dY - m_sPivotY].m_cDeadOwnerName));
				bEraseFlag = true;
			}
		}

	if (bEraseFlag != true) {
		for (dX = 0; dX < MAPDATASIZEX; dX++)
			for (dY = 0; dY < MAPDATASIZEY; dY++) {

				if (memcmp(m_pData[dX][dY].m_cDeadOwnerName, pTmpName, 10) == 0) {
					m_pData[dX][dY].m_sDeadOwnerType = 0;
					std::memset(m_pData[dX][dY].m_cDeadOwnerName, 0, sizeof(m_pData[dX][dY].m_cDeadOwnerName));
				}

			}
	}

	dX = sX - m_sPivotX;
	dY = sY - m_sPivotY;

	m_pData[dX][dY].m_wDeadObjectID = wObjectID;
	m_pData[dX][dY].m_sDeadOwnerType = sType;
	m_pData[dX][dY].m_cDeadDir = cDir;
	m_pData[dX][dY].m_deadAppearance = appearance;
	m_pData[dX][dY].m_deadStatus = status;
	m_pData[dX][dY].m_cDeadOwnerFrame = -1;
	strncpy_s(m_pData[dX][dY].m_cDeadOwnerName, sizeof(m_pData[dX][dY].m_cDeadOwnerName), pTmpName, _TRUNCATE);

	m_iObjectIDcacheLocX[wObjectID] = -1 * sX; //dX;
	m_iObjectIDcacheLocY[wObjectID] = -1 * sY; //dY;



	return true;
}

bool __fastcall CMapData::bSetChatMsgOwner(uint16_t wObjectID, short sX, short sY, int iIndex)
{
	int dX, dY;

	if ((sX == -10) && (sY == -10)) goto SCMO_FULL_SEARCH;

	if ((sX < m_sPivotX) || (sX >= m_sPivotX + MAPDATASIZEX) ||
		(sY < m_sPivotY) || (sY >= m_sPivotY + MAPDATASIZEY))
	{
		return false;
	}
	for (dX = sX - 4; dX <= sX + 4; dX++)
		for (dY = sY - 4; dY <= sY + 4; dY++)
		{
			if (dX < m_sPivotX) break;
			else
				if (dX > m_sPivotX + MAPDATASIZEX) break;
			if (dY < m_sPivotY) break;
			else
				if (dY > m_sPivotY + MAPDATASIZEY) break;

			if (m_pData[dX - m_sPivotX][dY - m_sPivotY].m_wObjectID == wObjectID) {
				m_pData[dX - m_sPivotX][dY - m_sPivotY].m_iChatMsg = iIndex;
				return true;
			}
			if (m_pData[dX - m_sPivotX][dY - m_sPivotY].m_wDeadObjectID == wObjectID) {
				m_pData[dX - m_sPivotX][dY - m_sPivotY].m_iDeadChatMsg = iIndex;
				return true;
			}
		}

SCMO_FULL_SEARCH:;

	for (dX = 0; dX < MAPDATASIZEX; dX++)
		for (dY = 0; dY < MAPDATASIZEY; dY++) {

			if (m_pData[dX][dY].m_wObjectID == wObjectID) {
				m_pData[dX][dY].m_iChatMsg = iIndex;
				return true;
			}
			if (m_pData[dX][dY].m_wDeadObjectID == wObjectID) {
				m_pData[dX][dY].m_iDeadChatMsg = iIndex;
				return true;
			}
		}

	return false;
}

void CMapData::ClearChatMsg(short sX, short sY)
{
	// v1.411
	if (m_pGame->m_pChatMsgList[m_pData[sX - m_sPivotX][sY - m_sPivotY].m_iChatMsg]) {
		m_pGame->m_pChatMsgList[m_pData[sX - m_sPivotX][sY - m_sPivotY].m_iChatMsg].reset();
	}

	m_pData[sX - m_sPivotX][sY - m_sPivotY].m_iChatMsg = 0;
}

void CMapData::ClearDeadChatMsg(short sX, short sY)
{
	m_pData[sX - m_sPivotX][sY - m_sPivotY].m_iDeadChatMsg = 0;
}

bool __fastcall CMapData::bGetOwner(short sX, short sY, char* pName, short* pOwnerType, PlayerStatus* pOwnerStatus, uint16_t* pObjectID)
{
	int dX, dY;

	if ((sX < m_sPivotX) || (sX > m_sPivotX + MAPDATASIZEX) ||
		(sY < m_sPivotY) || (sY > m_sPivotY + MAPDATASIZEY)) {
		std::memset(pName, 0, sizeof(pName));
		return false;
	}

	dX = sX - m_sPivotX;
	dY = sY - m_sPivotY;

	*pOwnerType = m_pData[dX][dY].m_sOwnerType;
	strncpy_s(pName, 12, m_pData[dX][dY].m_cOwnerName, _TRUNCATE);
	*pOwnerStatus = m_pData[dX][dY].m_status;
	*pObjectID = m_pData[dX][dY].m_wObjectID;

	return true;
}

bool CMapData::bSetDynamicObject(short sX, short sY, uint16_t wID, short sType, bool bIsEvent)
{
	int dX, dY, sPrevType;

	if ((sX < m_sPivotX) || (sX >= m_sPivotX + MAPDATASIZEX) ||
		(sY < m_sPivotY) || (sY >= m_sPivotY + MAPDATASIZEY))
	{
		return false;
	}

	dX = sX - m_sPivotX;
	dY = sY - m_sPivotY;

	sPrevType = m_pData[dX][dY].m_sDynamicObjectType;

	m_pData[dX][dY].m_sDynamicObjectType = sType;
	m_pData[dX][dY].m_cDynamicObjectFrame = rand() % 5;
	m_pData[dX][dY].m_dwDynamicObjectTime = GameClock::GetTimeMS();

	m_pData[dX][dY].m_cDynamicObjectData1 = 0;
	m_pData[dX][dY].m_cDynamicObjectData2 = 0;
	m_pData[dX][dY].m_cDynamicObjectData3 = 0;
	m_pData[dX][dY].m_cDynamicObjectData4 = 0;

	switch (sType) {
	case 0:
		if (sPrevType == DEF_DYNAMICOBJECT_FIRE)
		{
			m_pGame->m_pEffectManager->AddEffect(EffectType::RED_CLOUD_PARTICLES, (m_sPivotX + dX) * 32, (m_sPivotY + dY) * 32, 0, 0, 0, 0);
			m_pGame->m_pEffectManager->AddEffect(EffectType::RED_CLOUD_PARTICLES, (m_sPivotX + dX) * 32 + (10 - (rand() % 20)), (m_sPivotY + dY) * 32 + (20 - (rand() % 40)), 0, 0, 0, 0);
			m_pGame->m_pEffectManager->AddEffect(EffectType::RED_CLOUD_PARTICLES, (m_sPivotX + dX) * 32 + (10 - (rand() % 20)), (m_sPivotY + dY) * 32 + (20 - (rand() % 40)), 0, 0, 0, 0);
			m_pGame->m_pEffectManager->AddEffect(EffectType::RED_CLOUD_PARTICLES, (m_sPivotX + dX) * 32 + (10 - (rand() % 20)), (m_sPivotY + dY) * 32 + (20 - (rand() % 40)), 0, 0, 0, 0);
		}
		else if ((sPrevType == DEF_DYNAMICOBJECT_PCLOUD_BEGIN) || (sPrevType == DEF_DYNAMICOBJECT_PCLOUD_LOOP))
		{
			m_pData[dX][dY].m_sDynamicObjectType = DEF_DYNAMICOBJECT_PCLOUD_END;
			m_pData[dX][dY].m_cDynamicObjectFrame = 0;
			m_pData[dX][dY].m_dwDynamicObjectTime = GameClock::GetTimeMS();
		}
		break;

	case DEF_DYNAMICOBJECT_FISH:
		m_pData[dX][dY].m_cDynamicObjectData1 = (rand() % 40) - 20;
		m_pData[dX][dY].m_cDynamicObjectData2 = (rand() % 40) - 20;
		m_pData[dX][dY].m_cDynamicObjectData3 = (rand() % 10) - 5;
		m_pData[dX][dY].m_cDynamicObjectData4 = (rand() % 10) - 5;
		break;

	case DEF_DYNAMICOBJECT_PCLOUD_BEGIN:
		if (bIsEvent == false)
		{
			m_pData[dX][dY].m_sDynamicObjectType = DEF_DYNAMICOBJECT_PCLOUD_LOOP;
			m_pData[dX][dY].m_cDynamicObjectFrame = rand() % 8;
		}
		else m_pData[dX][dY].m_cDynamicObjectFrame = -1 * (rand() % 8);
		break;

	case DEF_DYNAMICOBJECT_ARESDENFLAG1:
		m_pData[dX][dY].m_cDynamicObjectFrame = (rand() % 4);
		break;

	case DEF_DYNAMICOBJECT_ELVINEFLAG1:
		m_pData[dX][dY].m_cDynamicObjectFrame = 4 + (rand() % 4);
		break;
	}
	return true;
}

void CMapData::GetOwnerStatusByObjectID(uint16_t wObjectID, char* pOwnerType, char* pDir, PlayerAppearance* pAppearance, PlayerStatus* pStatus, char* pName)
{
	int iX, iY;
	for (iX = 0; iX < MAPDATASIZEX; iX++)
		for (iY = 0; iY < MAPDATASIZEY; iY++)
			if (m_pData[iX][iY].m_wObjectID == wObjectID)
			{
				*pOwnerType = (char)m_pData[iX][iY].m_sOwnerType;
				*pDir = m_pData[iX][iY].m_animation.cDir;
				*pAppearance = m_pData[iX][iY].m_appearance;
				*pStatus = m_pData[iX][iY].m_status;
				strncpy_s(pName, 12, m_pData[iX][iY].m_cOwnerName, _TRUNCATE);
				return;
			}
}
