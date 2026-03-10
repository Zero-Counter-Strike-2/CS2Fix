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

#include "commands.h"
#include "common.h"
#include "ctimer.h"
#include "detours.h"
#include "engine/igameeventsystem.h"
#include "entity/cbaseentity.h"
#include "entity/cbasemodelentity.h"
#include "entity/ccsplayercontroller.h"
#include "entity/ccsplayerpawn.h"
#include "entity/ccsweaponbase.h"
#include "entity/cparticlesystem.h"
#include "entity/lights.h"
#include "networksystem/inetworkmessages.h"
#include "playermanager.h"
#include "recipientfilters.h"
#include "tier0/vprof.h"

#include "usermessages.pb.h"
#include "utils/entity.h"
#include "utlstring.h"
#include <ranges>

#include "tier0/memdbgon.h"

CConVar<bool> g_cvarEnableCommands("cs2f_commands_enable", FCVAR_NONE, "Whether to enable chat commands", false);
CConVar<bool> g_cvarEnableWeapons("cs2f_weapons_enable", FCVAR_NONE, "Whether to enable weapon commands", false);

// We need to use a helper function to avoid command macros accessing command list before its initialized
std::map<uint32, std::shared_ptr<CChatCommand>>& CommandList()
{
	static std::map<uint32, std::shared_ptr<CChatCommand>> commandList;
	return commandList;
}

int GetGrenadeAmmo(CCSPlayer_WeaponServices* pWeaponServices, const WeaponInfo_t* pWeaponInfo)
{
	if (!pWeaponServices || pWeaponInfo->m_eSlot != GEAR_SLOT_GRENADES)
		return -1;

	// TODO: look into molotov vs inc interaction
	if (strcmp(pWeaponInfo->m_pClass, "weapon_hegrenade") == 0)
		return pWeaponServices->m_iAmmo[AMMO_OFFSET_HEGRENADE];
	if (strcmp(pWeaponInfo->m_pClass, "weapon_molotov") == 0 || strcmp(pWeaponInfo->m_pClass, "weapon_incgrenade") == 0)
		return pWeaponServices->m_iAmmo[AMMO_OFFSET_MOLOTOV];
	if (strcmp(pWeaponInfo->m_pClass, "weapon_decoy") == 0)
		return pWeaponServices->m_iAmmo[AMMO_OFFSET_DECOY];
	if (strcmp(pWeaponInfo->m_pClass, "weapon_flashbang") == 0)
		return pWeaponServices->m_iAmmo[AMMO_OFFSET_FLASHBANG];
	if (strcmp(pWeaponInfo->m_pClass, "weapon_smokegrenade") == 0)
		return pWeaponServices->m_iAmmo[AMMO_OFFSET_SMOKEGRENADE];
	return -1;
}

int GetGrenadeAmmoTotal(CCSPlayer_WeaponServices* pWeaponServices)
{
	if (!pWeaponServices)
		return -1;

	int grenadeAmmoOffsets[] = {
		AMMO_OFFSET_HEGRENADE,
		AMMO_OFFSET_FLASHBANG,
		AMMO_OFFSET_SMOKEGRENADE,
		AMMO_OFFSET_DECOY,
		AMMO_OFFSET_MOLOTOV,
	};

	int totalGrenades = 0;
	for (int i = 0; i < (sizeof(grenadeAmmoOffsets) / sizeof(int)); i++)
		totalGrenades += pWeaponServices->m_iAmmo[grenadeAmmoOffsets[i]];

	return totalGrenades;
}

void ParseWeaponCommand(const CCommand& args, CCSPlayerController* player)
{
	if (!g_cvarEnableWeapons.Get() || !player || !player->m_hPawn())
		return;

	VPROF("ParseWeaponCommand");

	const auto pPawn = reinterpret_cast<CCSPlayerPawn*>(player->GetPawn());

	const char* command = args[0];
	if (!V_strncmp("c_", command, 2))
		command = command + 2;

	const auto pWeaponInfo = FindWeaponInfoByAlias(command);

	if (!pWeaponInfo || pWeaponInfo->m_nPrice == 0)
		return;

	if (pPawn->m_iHealth() <= 0 || pPawn->m_iTeamNum != CS_TEAM_CT)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You can only buy weapons when human.");
		return;
	}

	CCSPlayer_ItemServices* pItemServices = pPawn->m_pItemServices;
	CCSPlayer_WeaponServices* pWeaponServices = pPawn->m_pWeaponServices;

	// it can sometimes be null when player joined on the very first round?
	if (!pItemServices || !pWeaponServices)
		return;

	int money = player->m_pInGameMoneyServices->m_iAccount;

	if (money < pWeaponInfo->m_nPrice)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You can't afford %s! It costs $%i, you only have $%i", pWeaponInfo->m_pName, pWeaponInfo->m_nPrice, money);
		return;
	}

	static ConVarRefAbstract ammo_grenade_limit_default("ammo_grenade_limit_default"), ammo_grenade_limit_total("ammo_grenade_limit_total"), mp_weapons_allow_typecount("mp_weapons_allow_typecount");

	int iGrenadeLimitDefault = ammo_grenade_limit_default.GetInt();
	int iGrenadeLimitTotal = ammo_grenade_limit_total.GetInt();
	int iWeaponLimit = mp_weapons_allow_typecount.GetInt();

	if (pWeaponInfo->m_eSlot == GEAR_SLOT_GRENADES)
	{
		int iMatchingGrenades = GetGrenadeAmmo(pWeaponServices, pWeaponInfo);
		int iTotalGrenades = GetGrenadeAmmoTotal(pWeaponServices);

		if (iMatchingGrenades >= iGrenadeLimitDefault)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You cannot carry any more %ss (Max %i)", pWeaponInfo->m_pName, iGrenadeLimitDefault);
			return;
		}

		if (iTotalGrenades >= iGrenadeLimitTotal)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You cannot carry any more grenades (Max %i)", iGrenadeLimitTotal);
			return;
		}
	}

	int maxAmount;

	if (pWeaponInfo->m_nMaxAmount)
		maxAmount = pWeaponInfo->m_nMaxAmount;
	else if (pWeaponInfo->m_eSlot == GEAR_SLOT_GRENADES)
		maxAmount = iGrenadeLimitDefault;
	else
		maxAmount = iWeaponLimit == -1 ? 9999 : iWeaponLimit;

	CUtlVector<WeaponPurchaseCount_t>* weaponPurchases = pPawn->m_pActionTrackingServices->m_weaponPurchasesThisRound().m_weaponPurchases;
	bool found = false;
	FOR_EACH_VEC(*weaponPurchases, i)
	{
		WeaponPurchaseCount_t& purchase = (*weaponPurchases)[i];
		if (purchase.m_nItemDefIndex == pWeaponInfo->m_iItemDefinitionIndex)
		{
			// Note ammo_grenade_limit_total is not followed here, only for checking inventory space
			if (purchase.m_nCount >= maxAmount)
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You cannot buy any more %s (Max %i)", pWeaponInfo->m_pName, maxAmount);
				return;
			}
			purchase.m_nCount += 1;
			found = true;
			break;
		}
	}

	if (!found)
	{
		WeaponPurchaseCount_t purchase(pPawn, pWeaponInfo->m_iItemDefinitionIndex, 1);
		weaponPurchases->AddToTail(purchase);
	}

	if (pWeaponInfo->m_eSlot == GEAR_SLOT_RIFLE || pWeaponInfo->m_eSlot == GEAR_SLOT_PISTOL)
	{
		CUtlVector<CHandle<CBasePlayerWeapon>>* weapons = pWeaponServices->m_hMyWeapons();

		FOR_EACH_VEC(*weapons, i)
		{
			CBasePlayerWeapon* weapon = (*weapons)[i].Get();

			if (!weapon)
				continue;

			if (weapon->GetWeaponVData()->m_GearSlot() == pWeaponInfo->m_eSlot)
			{
				pWeaponServices->DropWeapon(weapon);
				break;
			}
		}
	}

	CBasePlayerWeapon* pWeapon = pItemServices->GiveNamedItemAws(pWeaponInfo->m_pClass);

	// Normally shouldn't be possible, but avoid issues in some edge cases
	if (!pWeapon)
		return;

	player->m_pInGameMoneyServices->m_iAccount = money - pWeaponInfo->m_nPrice;

	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You have purchased %s for $%i", pWeaponInfo->m_pName, pWeaponInfo->m_nPrice);
}

void WeaponCommandCallback(const CCommandContext& context, const CCommand& args)
{
	CCSPlayerController* pController = nullptr;
	if (context.GetPlayerSlot().Get() != -1)
		pController = (CCSPlayerController*)g_pEntitySystem->GetEntityInstance((CEntityIndex)(context.GetPlayerSlot().Get() + 1));

	// Only allow connected players to run chat commands
	if (pController && !pController->IsConnected())
		return;

	ParseWeaponCommand(args, pController);
}

void RegisterWeaponCommands()
{
	const auto& weapons = GenerateWeaponCommands();

	for (const auto& aliases : weapons | std::views::values)
	{
		for (const auto& alias : aliases)
		{
			CChatCommand::Create(alias.c_str(), ParseWeaponCommand, "- Buys this weapon", ADMFLAG_NONE, CMDFLAG_NOHELP);

			char cmdName[64];
			V_snprintf(cmdName, sizeof(cmdName), "%s%s", COMMAND_PREFIX, alias.c_str());

			new ConCommand(cmdName, WeaponCommandCallback, "Buys this weapon", FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_LINKED_CONCOMMAND);
		}
	}
}

void ParseChatCommand(const char* pMessage, CCSPlayerController* pController)
{
	if (!pController || !pController->IsConnected())
		return;

	VPROF("ParseChatCommand");

	CCommand args;
	args.Tokenize(pMessage);
	std::string name = args[0];

	for (int i = 0; name[i]; i++)
		name[i] = tolower(name[i]);

	uint32 nameHash = hash_32_fnv1a_const(name.c_str());

	if (CommandList().contains(nameHash))
		(*CommandList()[nameHash])(args, pController);
}

bool CChatCommand::CheckCommandAccess(CCSPlayerController* pPlayer, uint64 flags)
{
	if (!pPlayer)
		return false;

	int slot = pPlayer->GetPlayerSlot();

	ZEPlayer* pZEPlayer = g_playerManager->GetPlayer(slot);

	if (!pZEPlayer)
		return false;

	// With admin system removed, allow all non-admin commands
	if (flags != ADMFLAG_NONE)
		return false;

	return true;
}

void ClientPrintAll(int hud_dest, const char* msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);

	va_end(args);

	INetworkMessageInternal* pNetMsg = g_pNetworkMessages->FindNetworkMessagePartial("TextMsg");
	auto data = pNetMsg->AllocateMessage()->ToPB<CUserMessageTextMsg>();

	data->set_dest(hud_dest);
	data->add_param(buf);

	CRecipientFilter filter;
	filter.AddAllPlayers();

	g_gameEventSystem->PostEventAbstract(-1, false, &filter, pNetMsg, data, 0);

	delete data;

	ConMsg("%s\n", buf);
}

void ClientPrint(CCSPlayerController* player, int hud_dest, const char* msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);

	va_end(args);

	if (!player || !player->IsConnected() || player->IsBot())
	{
		ConMsg("%s\n", buf);
		return;
	}

	INetworkMessageInternal* pNetMsg = g_pNetworkMessages->FindNetworkMessagePartial("TextMsg");
	auto data = pNetMsg->AllocateMessage()->ToPB<CUserMessageTextMsg>();

	data->set_dest(hud_dest);
	data->add_param(buf);

	CSingleRecipientFilter filter(player->GetPlayerSlot());

	g_gameEventSystem->PostEventAbstract(-1, false, &filter, pNetMsg, data, 0);

	delete data;
}

CConVar<bool> g_cvarEnableStopSound("cs2f_stopsound_enable", FCVAR_NONE, "Whether to enable stopsound", false);
CConVar<bool> g_cvarEnableNoShake("cs2f_noshake_enable", FCVAR_NONE, "Whether to enable noshake command", false);
CConVar<float> g_cvarMaxShakeAmp("cs2f_maximum_shake_amplitude", FCVAR_NONE, "Shaking Amplitude bigger than this will be clamped", -1.0f, true, -1.0f, true, 16.0f);
CConVar<bool> g_cvarEnableHide("cs2f_hide_enable", FCVAR_NONE, "Whether to enable hide (WARNING: randomly crashes clients since 2023-12-13 CS2 update)", false);
CConVar<bool> g_cvarHideWeapons("cs2f_hide_weapons", FCVAR_NONE, "Whether to hide weapons along with their holders", false);

void PrintHelp(const CCommand& args, CCSPlayerController* player)
{
	std::vector<std::string> rgstrCommands;
	if (args.ArgC() < 2)
	{
		if (!player)
		{
			ClientPrint(player, HUD_PRINTCONSOLE, "The list of all commands is:");

			for (const auto& cmdPair : CommandList())
			{
				auto cmd = cmdPair.second;

				if (!cmd->IsCommandFlagSet(CMDFLAG_NOHELP))
					rgstrCommands.push_back(std::string("c_") + cmd->GetName() + " " + cmd->GetDescription());
			}
		}
		else
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "The list of all available commands will be shown in console.");
			ClientPrint(player, HUD_PRINTCONSOLE, "The list of all commands you can use is:");

			for (const auto& cmdPair : CommandList())
			{
				auto cmd = cmdPair.second;

				if (!cmd->IsCommandFlagSet(CMDFLAG_NOHELP))
					rgstrCommands.push_back(std::string("!") + cmd->GetName() + " " + cmd->GetDescription());
			}
		}
	}
	else
	{
		const char* pszSearchTerm = args[1];
		bool bOnlyCheckStart = false;

		if (V_strnicmp(args[1], "c_", 2) == 0)
		{
			bOnlyCheckStart = true;
			pszSearchTerm++;
			pszSearchTerm++;
		}
		else if (V_strnicmp(args[1], "!", 1) == 0 || V_strnicmp(args[1], "/", 1) == 0)
		{
			bOnlyCheckStart = true;
			pszSearchTerm++;
		}

		for (const auto& cmdPair : CommandList())
		{
			auto cmd = cmdPair.second;

			if (!cmd->IsCommandFlagSet(CMDFLAG_NOHELP)
				&& ((!bOnlyCheckStart && V_stristr(cmd->GetName(), pszSearchTerm))
					|| (bOnlyCheckStart && V_strnicmp(cmd->GetName(), pszSearchTerm, strlen(pszSearchTerm)) == 0)))
			{
				if (player)
					rgstrCommands.push_back(std::string("!") + cmd->GetName() + " " + cmd->GetDescription());
				else
					rgstrCommands.push_back(std::string("c_") + cmd->GetName() + " " + cmd->GetDescription());
			}
		}

		if (rgstrCommands.size() == 0)
		{
			ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "No commands matched \"%s\".", args[1]);
			if (player)
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "No commands matched \"%s\".", args[1]);
			return;
		}

		ClientPrint(player, HUD_PRINTCONSOLE, "The list of all commands matching \"%s\" is:", args[1]);
		if (player)
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "The list of all commands matching \"%s\" will be shown in console.", args[1]);
	}

	std::sort(rgstrCommands.begin(), rgstrCommands.end());

	for (const auto& strCommand : rgstrCommands)
		ClientPrint(player, HUD_PRINTCONSOLE, strCommand.c_str());

	if (player)
		ClientPrint(player, HUD_PRINTCONSOLE, "! can be replaced with / for a silent chat command, or c_ for console usage");
}

CON_COMMAND_CHAT(help, "- Display list of commands in console")
{
	PrintHelp(args, player);
}

CON_COMMAND_CHAT(find, "<text> - Search for specific commands and list them in console")
{
	PrintHelp(args, player);
}

CON_COMMAND_CHAT(getpos, "- Get your position and angles")
{
	if (!player)
		return;

	Vector vecAbsOrigin = player->GetPawn()->GetAbsOrigin();
	QAngle angRotation = player->GetPawn()->GetAbsRotation();

	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "setpos %f %f %f;setang %f %f %f", vecAbsOrigin.x, vecAbsOrigin.y, vecAbsOrigin.z, angRotation.x, angRotation.y, angRotation.z);
	ClientPrint(player, HUD_PRINTCONSOLE, "setpos %f %f %f;setang %f %f %f", vecAbsOrigin.x, vecAbsOrigin.y, vecAbsOrigin.z, angRotation.x, angRotation.y, angRotation.z);
}
