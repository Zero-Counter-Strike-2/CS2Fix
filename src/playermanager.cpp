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

#include "playermanager.h"
#include "commands.h"
#include "ctimer.h"
#include "engine/igameeventsystem.h"
#include "entity/ccsplayercontroller.h"
#include "serversideclient.h"
#include <../cs2fixes.h>

#include "tier0/memdbgon.h"

CPlayerManager* g_playerManager = nullptr;

CConVar<bool> g_cvarEnableMapSteamIds("cs2f_map_steamids_enable", FCVAR_NONE, "Whether to make Steam ID's available to maps", false);

ZEPlayerHandle::ZEPlayerHandle() :
	m_Index(INVALID_ZEPLAYERHANDLE_INDEX) {};

ZEPlayerHandle::ZEPlayerHandle(CPlayerSlot slot)
{
	m_Parts.m_PlayerSlot = slot.Get();
	m_Parts.m_Serial = ++iZEPlayerHandleSerial;
}

ZEPlayerHandle::ZEPlayerHandle(const ZEPlayerHandle& other)
{
	m_Index = other.m_Index;
}

ZEPlayerHandle::ZEPlayerHandle(ZEPlayer* pZEPlayer)
{
	Set(pZEPlayer);
}

bool ZEPlayerHandle::operator==(ZEPlayer* pZEPlayer) const
{
	return Get() == pZEPlayer;
}

bool ZEPlayerHandle::operator!=(ZEPlayer* pZEPlayer) const
{
	return Get() != pZEPlayer;
}

void ZEPlayerHandle::Set(ZEPlayer* pZEPlayer)
{
	if (pZEPlayer)
		m_Index = pZEPlayer->GetHandle().m_Index;
	else
		m_Index = INVALID_ZEPLAYERHANDLE_INDEX;
}

ZEPlayer* ZEPlayerHandle::Get() const
{
	ZEPlayer* pZEPlayer = g_playerManager->GetPlayer((CPlayerSlot)m_Parts.m_PlayerSlot);

	if (!pZEPlayer)
		return nullptr;

	if (pZEPlayer->GetHandle().m_Index != m_Index)
		return nullptr;

	return pZEPlayer;
}

void ZEPlayer::OnSpawn()
{
	SetSpeedMod(1.f);
}

void ZEPlayer::OnAuthenticated()
{
	m_bAuthenticated = true;
	m_SteamID = m_UnauthenticatedSteamID;

	Message("%lli authenticated\n", GetSteamId64());

	SetSteamIdAttribute();
}

void ZEPlayer::SetSteamIdAttribute()
{
	if (!g_cvarEnableMapSteamIds.Get())
		return;

	if (!IsAuthenticated())
		return;

	const auto pController = CCSPlayerController::FromSlot(GetPlayerSlot());
	if (!pController || !pController->IsConnected() || pController->IsBot() || pController->m_bIsHLTV())
		return;

	const auto pPawn = pController->GetPlayerPawn();
	if (!pPawn)
		return;

	const auto& steamId = std::to_string(GetSteamId64());
	pPawn->AcceptInput("AddAttribute", steamId.c_str());
	pController->AcceptInput("AddAttribute", steamId.c_str());
}

ZEPlayer* CPlayerManager::GetPlayer(CPlayerSlot slot)
{
	if (slot.Get() < 0 || slot.Get() >= MAXPLAYERS)
		return nullptr;

	return m_vecPlayers[slot.Get()];
}

void CPlayerManager::OnBotConnected(CPlayerSlot slot)
{
	m_vecPlayers[slot.Get()] = new ZEPlayer(slot, true);
}

bool CPlayerManager::OnClientConnected(CPlayerSlot slot, uint64 xuid, const char* pszNetworkID)
{
	Assert(m_vecPlayers[slot.Get()] == nullptr);

	Message("%d connected\n", slot.Get());

	ZEPlayer* pPlayer = new ZEPlayer(slot);
	pPlayer->SetUnauthenticatedSteamId(new CSteamID(xuid));

	// Sometimes clients can be already auth'd at this point
	if (g_pEngineServer2->IsClientFullyAuthenticated(slot))
		pPlayer->OnAuthenticated();

	pPlayer->SetConnected();
	m_vecPlayers[slot.Get()] = pPlayer;

	return true;
}

void CPlayerManager::OnClientDisconnect(CPlayerSlot slot)
{
	Message("%d disconnected\n", slot.Get());

	delete m_vecPlayers[slot.Get()];
	m_vecPlayers[slot.Get()] = nullptr;
}

void CPlayerManager::OnClientPutInServer(CPlayerSlot slot)
{
	ZEPlayer* pPlayer = m_vecPlayers[slot.Get()];

	if (!pPlayer)
		return;

	pPlayer->SetInGame(true);

	if (!g_pSpawnGroupMgr)
		return;

	CUtlVector<SpawnGroupHandle_t> vecActualSpawnGroups;
	addresses::GetSpawnGroups(g_pSpawnGroupMgr, &vecActualSpawnGroups);

	CServerSideClient* pClient = GetClientBySlot(slot);

	if (pClient && pClient->m_vecLoadedSpawnGroups.Count() != vecActualSpawnGroups.Count())
		pClient->m_vecLoadedSpawnGroups = vecActualSpawnGroups;
}

void CPlayerManager::OnLateLoad()
{
	if (!GetGlobals())
		return;

	for (int i = 0; i < GetGlobals()->maxClients; i++)
	{
		CCSPlayerController* pController = CCSPlayerController::FromSlot(i);

		if (!pController || !pController->IsController() || !pController->IsConnected())
			continue;

		OnClientConnected(i, pController->m_steamID(), "0.0.0.0:0");
	}
}

void CPlayerManager::OnSteamAPIActivated()
{
	m_CallbackValidateAuthTicketResponse.Register(this, &CPlayerManager::OnValidateAuthTicket);
}

void CPlayerManager::OnValidateAuthTicket(ValidateAuthTicketResponse_t* pResponse)
{
	uint64 iSteamId = pResponse->m_SteamID.ConvertToUint64();

	Message("%s: SteamID=%llu Response=%d\n", __func__, iSteamId, pResponse->m_eAuthSessionResponse);

	for (ZEPlayer* pPlayer : m_vecPlayers)
	{
		if (!pPlayer || pPlayer->IsFakeClient() || !(pPlayer->GetUnauthenticatedSteamId64() == iSteamId))
			continue;

		if (pResponse->m_eAuthSessionResponse == k_EAuthSessionResponseOK)
		{
			pPlayer->OnAuthenticated();
			return;
		}
	}
}
