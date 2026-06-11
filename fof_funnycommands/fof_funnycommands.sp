#include <sourcemod>
#include <sdktools>

public Plugin myinfo =
{
    name = "[FoF] Funny Commands",
    author = "",
    version = "1.0.0",
    description = "FoF Funny Commands",
    url = ""
};

public OnPluginStart()
{
	RegAdminCmd("sm_fof_giveitem", Command_GiveItem, ADMFLAG_GENERIC, "GiveItem Command");
	RegAdminCmd("sm_fof_setspeed", Command_SetSpeed, ADMFLAG_GENERIC, "SetSpeed Command");
}

public Action Command_GiveItem(client, args)
{
	if (args < 2)
        ReplyToCommand(client, "[SM] Usage: sm_fof_giveitem <userid> <itemname>");
	else
	{
		char item[32];
		char target[32];
		char target_name[32];
		int target_list[MAXPLAYERS];
		int target_count;
		bool tn_is_ml;
		GetCmdArg(1, target, sizeof(target));
		GetCmdArg(2, item, sizeof(item));
		if ((target_count = ProcessTargetString(target, client, target_list, MAXPLAYERS, COMMAND_FILTER_ALIVE, target_name, sizeof(target_name), tn_is_ml)) <= 0)
		{
			ReplyToTargetError(client, target_count);
			return Plugin_Handled;
		}
		for (int i = 0; i < target_count; i++)
			GivePlayerItem(target_list[i], item);
		ReplyToCommand(client, "[SM] Gave player %s %s.", target_name, item);
	}
	return Plugin_Handled;
}

public Action Command_SetSpeed(client, args)
{
	if (args < 2)
        ReplyToCommand(client, "[SM] Usage: sm_fof_setspeed <userid> <speed>");
	else
	{
		char value[16];
		char target[32];
		char target_name[32];
		int target_list[MAXPLAYERS];
		int target_count;
		bool tn_is_ml;
		GetCmdArg(1, target, sizeof(target));
		GetCmdArg(2, value, sizeof(value));
		new Float:fvalue = StringToFloat(value);
		if ((target_count = ProcessTargetString(target, client, target_list, MAXPLAYERS, COMMAND_FILTER_ALIVE, target_name, sizeof(target_name), tn_is_ml)) <= 0)
		{
			ReplyToTargetError(client, target_count);
			return Plugin_Handled;
		}
		for (int i = 0; i < target_count; i++)
			SetEntPropFloat(target_list[i], Prop_Data, "m_flLaggedMovementValue", fvalue);
		ReplyToCommand(client, "[SM] Set player speed %s %f.", target_name, fvalue);
	}
	return Plugin_Handled;
}
