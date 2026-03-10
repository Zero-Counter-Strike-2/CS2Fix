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

#pragma once
#include "common.h"
#include "gamesystem.h"
#include "steam/isteamuser.h"
#include "steam/steam_api_common.h"
#include "steam/steamclientpublic.h"
#include "utlvector.h"
#include <playerslot.h>

#define INVALID_ZEPLAYERHANDLE_INDEX 0u

static uint32 iZEPlayerHandleSerial = 0u;

class ZEPlayer;

class ZEPlayerHandle
{
public:
	ZEPlayerHandle();
	ZEPlayerHandle(CPlayerSlot slot);
	ZEPlayerHandle(const ZEPlayerHandle& other);
	ZEPlayerHandle(ZEPlayer* pZEPlayer);

	bool IsValid() const { return static_cast<bool>(Get()); }

	uint32 GetIndex() const { return m_Index; }
	uint32 GetPlayerSlot() const { return m_Parts.m_PlayerSlot; }
	uint32 GetSerial() const { return m_Parts.m_Serial; }

	bool operator==(const ZEPlayerHandle& other) const { return other.m_Index == m_Index; }
	bool operator!=(const ZEPlayerHandle& other) const { return other.m_Index != m_Index; }
	bool operator==(ZEPlayer* pZEPlayer) const;
	bool operator!=(ZEPlayer* pZEPlayer) const;

	void operator=(const ZEPlayerHandle& other) { m_Index = other.m_Index; }
	void operator=(ZEPlayer* pZEPlayer) { Set(pZEPlayer); }
	void Set(ZEPlayer* pZEPlayer);

	ZEPlayer* Get() const;

private:
	union
	{
		uint32 m_Index;
		struct
		{
			uint32 m_PlayerSlot : 6;
			uint32 m_Serial : 26;
		} m_Parts;
	};
};

class ZEPlayer
{
public:
	ZEPlayer(CPlayerSlot slot, bool m_bFakeClient = false) :
		m_slot(slot), m_bFakeClient(m_bFakeClient), m_Handle(slot)
	{
		m_bAuthenticated = false;
		m_SteamID = nullptr;
		m_UnauthenticatedSteamID = nullptr;
		m_bConnected = false;
		m_bInGame = false;
		m_flSpeedMod = 1.f;
		m_flMaxSpeed = 1.f;
	}

	bool IsFakeClient() { return m_bFakeClient; }
	bool IsAuthenticated() { return m_bAuthenticated; }
	bool IsConnected() { return m_bConnected; }
	uint64 GetUnauthenticatedSteamId64() { return m_UnauthenticatedSteamID->ConvertToUint64(); }
	const CSteamID* GetUnauthenticatedSteamId() { return m_UnauthenticatedSteamID; }
	uint64 GetSteamId64() { return m_SteamID->ConvertToUint64(); }
	const CSteamID* GetSteamId() { return m_SteamID; }

	void SetConnected() { m_bConnected = true; }
	void SetUnauthenticatedSteamId(const CSteamID* steamID) { m_UnauthenticatedSteamID = steamID; }
	void SetSteamId(const CSteamID* steamID) { m_SteamID = steamID; }
	void SetPlayerSlot(CPlayerSlot slot) { m_slot = slot; }
	void SetSpeedMod(float flSpeedMod) { m_flSpeedMod = flSpeedMod; }
	void SetMaxSpeed(float flMaxSpeed) { m_flMaxSpeed = flMaxSpeed; }
	void SetInGame(bool bInGame) { m_bInGame = bInGame; }

	CPlayerSlot GetPlayerSlot() { return m_slot; }
	ZEPlayerHandle GetHandle() { return m_Handle; }
	float GetSpeedMod() { return m_flSpeedMod; }
	float GetMaxSpeed() { return m_flMaxSpeed; }
	bool IsInGame() { return m_bInGame; }

	void OnSpawn();
	void OnAuthenticated();
	void SetSteamIdAttribute();

private:
	bool m_bAuthenticated;
	bool m_bConnected;
	const CSteamID* m_UnauthenticatedSteamID;
	const CSteamID* m_SteamID;
	CPlayerSlot m_slot;
	bool m_bFakeClient;
	bool m_bInGame;
	float m_flSpeedMod;
	float m_flMaxSpeed;
	ZEPlayerHandle m_Handle;
};

class CPlayerManager
{
public:
	CPlayerManager()
	{
		V_memset(m_vecPlayers, 0, sizeof(m_vecPlayers));
	}

	bool OnClientConnected(CPlayerSlot slot, uint64 xuid, const char* pszNetworkID);
	void OnClientDisconnect(CPlayerSlot slot);
	void OnBotConnected(CPlayerSlot slot);
	void OnClientPutInServer(CPlayerSlot slot);
	void OnLateLoad();
	void OnSteamAPIActivated();

	ZEPlayer* GetPlayer(CPlayerSlot slot);

	STEAM_GAMESERVER_CALLBACK_MANUAL(CPlayerManager, OnValidateAuthTicket, ValidateAuthTicketResponse_t, m_CallbackValidateAuthTicketResponse);

private:
	ZEPlayer* m_vecPlayers[MAXPLAYERS];
};

extern CPlayerManager* g_playerManager;
