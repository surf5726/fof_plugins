#include <sourcemod>

Handle g_Cvar1 = INVALID_HANDLE;
Handle g_Cvar2 = INVALID_HANDLE;
Handle g_Cvar3 = INVALID_HANDLE;

public Plugin myinfo =
{
	name = "[FoF] Disable Auto Teambalance",
	description = "Disable Auto Teambalance",
	author = "",
	version = "0.1.0",
	url = ""
}

public void OnPluginStart()
{
	g_Cvar1 = FindConVar("fof_sv_teambalance_allowed");
	g_Cvar2 = FindConVar("mp_teams_unbalance_limit");
	g_Cvar3 = FindConVar("mp_autoteambalance");
	HookConVarChange(g_Cvar1, ConVarChange_Cvar1);
	HookConVarChange(g_Cvar2, ConVarChange_Cvar2);
	HookConVarChange(g_Cvar3, ConVarChange_Cvar3);
}

public void ConVarChange_Cvar1(Handle convar, const char[] oldValue, const char[] newValue)
{
	if (StringToInt(oldValue) != 0 || StringToInt(newValue) != 0)
		ServerCommand("fof_sv_teambalance_allowed 0");
}

public void ConVarChange_Cvar2(Handle convar, const char[] oldValue, const char[] newValue)
{
	if (StringToInt(oldValue) != 0 || StringToInt(newValue) != 0)
		ServerCommand("mp_teams_unbalance_limit 0");
}

public void ConVarChange_Cvar3(Handle convar, const char[] oldValue, const char[] newValue)
{
	if (StringToInt(oldValue) != 0 || StringToInt(newValue) != 0)
		ServerCommand("mp_autoteambalance 0");
}