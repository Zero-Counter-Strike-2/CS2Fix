/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023-2026 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "KeyValues.h"
#include "commands.h"
#include "common.h"
#include "cs2fixes.h"
#include "ctimer.h"
#include "entities.h"
#include "entity.h"
#include "entity/cbaseplayercontroller.h"
#include "entity/cgamerules.h"
#include "eventlistener.h"
#include "recipientfilters.h"

#include "tier0/memdbgon.h"

static CConVar<bool> g_cvarFixHudFlashing("cs2f_fix_hud_flashing", FCVAR_NONE, "Whether to fix the HUD flashing after round end", false);

CUtlVector<CGameEventListener*> g_vecEventListeners;

void RegisterEventListeners()
{
	static bool bRegistered = false;

	if (bRegistered || !g_gameEventManager)
		return;

	FOR_EACH_VEC(g_vecEventListeners, i)
	{
		g_gameEventManager->AddListener(g_vecEventListeners[i], g_vecEventListeners[i]->GetEventName(), true);
	}

	bRegistered = true;
}

void UnregisterEventListeners()
{
	if (!g_gameEventManager)
		return;

	FOR_EACH_VEC(g_vecEventListeners, i)
	{
		g_gameEventManager->RemoveListener(g_vecEventListeners[i]);
	}

	g_vecEventListeners.Purge();
}

GAME_EVENT_F(round_prestart)
{
	RemoveTimers(TIMERFLAG_ROUND);

	EntityHandler_OnRoundRestart();

	CBaseEntity* pShake = nullptr;

	// Prevent shakes carrying over from previous rounds
	while ((pShake = UTIL_FindEntityByClassname(pShake, "env_shake")))
		pShake->AcceptInput("StopShake");
}

CConVar<bool> g_cvarBlockTeamMessages("cs2f_block_team_messages", FCVAR_NONE, "Whether to block team join messages", false);

GAME_EVENT_F(player_team)
{
	// Remove chat message for team changes
	if (g_cvarBlockTeamMessages.Get())
		pEvent->SetBool("silent", true);
}

CConVar<bool> g_cvarNoblock("cs2f_noblock_enable", FCVAR_NONE, "Whether to use player noblock, which sets debris collision on every player", false);
CConVar<int> g_cvarFreeArmor("cs2f_free_armor", FCVAR_NONE, "Whether kevlar (1+) and/or helmet (2) are given automatically", 0, true, 0, true, 2);

GAME_EVENT_F(player_spawn)
{
	CCSPlayerController* pController = (CCSPlayerController*)pEvent->GetPlayerController("userid");

	if (!pController)
		return;

	ZEPlayer* pPlayer = pController->GetZEPlayer();

	// always reset when player spawns
	if (pPlayer)
		pPlayer->SetMaxSpeed(1.f);

	if (pController->IsConnected())
		pController->GetZEPlayer()->OnSpawn();

	CHandle<CCSPlayerController> hController = pController->GetHandle();

	// Gotta do this on the next frame...
	CTimer::Create(0.0f, TIMERFLAG_MAP | TIMERFLAG_ROUND, [hController]() {
		CCSPlayerController* pController = hController.Get();

		if (!pController)
			return -1.0f;

		if (const auto player = pController->GetZEPlayer())
			player->SetSteamIdAttribute();

		if (!pController->m_bPawnIsAlive())
			return -1.0f;

		CBasePlayerPawn* pPawn = pController->GetPawn();

		// Just in case somehow there's health but the player is, say, an observer
		if (!g_cvarNoblock.Get() || !pPawn || !pPawn->IsAlive())
			return -1.0f;

		pPawn->SetCollisionGroup(COLLISION_GROUP_DEBRIS);

		return -1.0f;
	});

	CCSPlayerPawn* pPawn = (CCSPlayerPawn*)pController->GetPawn();

	if (!pPawn)
		return;

	CCSPlayer_ItemServices* pItemServices = pPawn->m_pItemServices();

	if (!pItemServices)
		return;

	// Dumb workaround for mp_free_armor breaking kevlar rebuys in buy menu
	if (g_cvarFreeArmor.GetInt() == 1)
		pItemServices->GiveNamedItem("item_kevlar");
	else if (g_cvarFreeArmor.GetInt() == 2)
		pItemServices->GiveNamedItem("item_assaultsuit");
}

GAME_EVENT_F(player_hurt)
{
}

GAME_EVENT_F(player_death)
{
}

CConVar<bool> g_cvarFullAllTalk("cs2f_full_alltalk", FCVAR_NONE, "Whether to enforce sv_full_alltalk 1", false);

GAME_EVENT_F(round_start)
{
	// Dumb workaround for CS2 always overriding sv_full_alltalk on state changes
	if (g_cvarFullAllTalk.Get())
		g_pEngineServer2->ServerCommand("sv_full_alltalk 1");

	// Ensure there's no warmup, because mp_warmup_online_enabled gets randomly ignored for some reason, this is a problem with cs2f_fix_hud_flashing
	if (g_cvarFixHudFlashing.Get() && g_pGameRules && g_pGameRules->m_bWarmupPeriod)
		g_pEngineServer2->ServerCommand("mp_warmup_end");
}

GAME_EVENT_F(round_end)
{
	if (g_cvarFixHudFlashing.Get() && g_pGameRules)
		g_pGameRules->m_bGameRestart = false;
}

GAME_EVENT_F(round_freeze_end)
{
}

GAME_EVENT_F(round_time_warning)
{
}

GAME_EVENT_F(bullet_impact)
{
}

GAME_EVENT_F(vote_cast)
{
}

GAME_EVENT_F(cs_win_panel_match)
{
}