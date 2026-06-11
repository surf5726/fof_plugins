#include <sourcemod>
#include <sdkhooks>
#include <smlib>

bool isRoundStart;
bool isClientInvisible[MAXPLAYERS + 1];
float lastClientSwitchVisiblityTime[MAXPLAYERS + 1];
float lastClientSpawnPropTime[MAXPLAYERS + 1];

public Plugin myinfo =
{
	name = "[FoF] WTFMode",
	author = "",
	description = "LoL",
	version = "1.0.0",
	url = ""
}

public void OnPluginStart()
{
	RegConsoleCmd("sm_wtfhelp", Command_GetHelp);
	HookEvent("round_start", Event_RoundStart);
	HookEvent("player_shoot", Event_PlayerShoot);
}

public void OnClientPutInServer(int client)
{
	if (IsValidClient(client))
	{
		PrintToChat(client, "Type !wtfhelp to get help information");
		SDKHook(client, SDKHook_WeaponSwitchPost, OnWeaponSwitchPost);
		SDKHook(client, SDKHook_OnTakeDamage, OnTakeDamage);
	}
}

public void OnClientDisconnect(int client)
{
	if (IsValidClient(client))
	{
		if (lastClientSwitchVisiblityTime[client] != 0.0)
			lastClientSwitchVisiblityTime[client] = 0.0;
		if (lastClientSpawnPropTime[client] != 0.0)
			lastClientSpawnPropTime[client] = 0.0;
		if (isClientInvisible[client])
			isClientInvisible[client] = false;
		SDKUnhook(client, SDKHook_WeaponSwitchPost, OnWeaponSwitchPost);
		SDKUnhook(client, SDKHook_OnTakeDamage, OnTakeDamage);
	}
}

public void Event_RoundStart(Event event, const char[] name, bool dontBroadcast)
{
	isRoundStart = true;
}

public void Event_PlayerShoot(Event event, const char[] name, bool dontBroadcast)
{
	if (isRoundStart)
	{
		int client = GetClientOfUserId(GetEventInt(event, "userid"));
		if (IsValidClient(client))
		{
			int activeWeapon = GetEntPropEnt(client, Prop_Send, "m_hActiveWeapon");
			int activeWeapon2 = GetEntPropEnt(client, Prop_Send, "m_hActiveWeapon2");
			if (activeWeapon2 == -1 && ClientWeaponEqual(client, "weapon_deringer"))
				ThrowProp(client);
			else if (activeWeapon2 == -1 && ClientWeaponEqual(client, "weapon_ghostgun"))
			{
				char clientName[255];
				GetClientName(client, clientName, sizeof(clientName));
				PrintCenterTextAll("%s launched a nuclear bomb!", clientName);
				ThrowProp(client, "99999", "99999");
				DataPack dataPack;
				CreateDataTimer(0.1, DataTimer_RemoveNuclearWeapon, dataPack);
				WritePackCell(dataPack, client);
				WritePackCell(dataPack, activeWeapon);
			}
		}
	}
}

public Action Command_GetHelp(int client, int args)
{
	if (IsValidClient(client))
	{
		PrintToChat(client, "Gameplay Overview");
		PrintToChat(client, "--------");
		PrintToChat(client, "Right Mini: Left click to launch oil barrels/Right click to place oil barrels (places oil barrels every 5 seconds)");
		PrintToChat(client, "Fist/Brass Knuckles: King of Fighters (Strike Speed++)");
		PrintToChat(client, "Fist/Brass Knuckles + Aim at an enemy and hold down Kick: Teleport to the enemy and kick them into the air");
		PrintToChat(client, "Simple Kick: Simply kick them into the air");
		PrintToChat(client, "Right Volcano: 150 damage");
		PrintToChat(client, "Right Short Spray: Strong knockback (the farther away from the enemy, the better; it won't work if you're too close)");
		PrintToChat(client, "Axe/Machete: Middle mouse button toggles stealth/non-stealth (toggles every 5 seconds)");
		PrintToChat(client, "--------");
	}
	return Plugin_Handled;
}

public Action OnPlayerRunCmd(int client, int &buttons, int &impulse, float vel[3], float angles[3], int &weapon)
{
	if (isRoundStart)
	{
		if (IsValidClient(client))
		{
			if (buttons & IN_ATTACK)
			{
				float gameTime = GetGameTime();
				//new viewModel = GetEntPropEnt(client, Prop_Send, "m_hViewModel");
				if (ClientWeaponEqual(client, "weapon_fists") || ClientWeaponEqual(client, "weapon_fists_brass"))
				{
					int activeWeapon = GetEntPropEnt(client, Prop_Send, "m_hActiveWeapon");
					float hitDelay = GetEntPropFloat(activeWeapon, Prop_Send, "m_flHitDelay");
					if (gameTime - hitDelay > hitDelay)
						SetEntPropFloat(activeWeapon, Prop_Send, "m_flHitDelay", gameTime + 0.1);
				}
			}
			else if (buttons & IN_ATTACK2)
			{
				float gameTime = GetGameTime();
				int activeWeapon2 = GetEntPropEnt(client, Prop_Send, "m_hActiveWeapon2");
				if (activeWeapon2 == -1 && ClientWeaponEqual(client, "weapon_deringer") && gameTime >= lastClientSpawnPropTime[client])
				{
					lastClientSpawnPropTime[client] = gameTime + 5.0;
					SpawnProp(client);
				}
			}
			else if (buttons & IN_ALT1)
			{
				float gameTime = GetGameTime();
				int activeWeapon = GetEntPropEnt(client, Prop_Send, "m_hActiveWeapon");
				if ((ClientWeaponEqual(client, "weapon_axe") || ClientWeaponEqual(client, "weapon_machete")) && gameTime >= lastClientSwitchVisiblityTime[client])
				{
					lastClientSwitchVisiblityTime[client] = gameTime + 5.0;
					if (!isClientInvisible[client])
					{
						isClientInvisible[client] = true;
						PrintCenterText(client, "You have entered incognito mode");
						SetClientAndWeaponInvisible(client, activeWeapon, true);
						SetClientSpeedAndGravity(client, 1.0, 0.5);
					}
					else
					{
						isClientInvisible[client] = false;
						PrintCenterText(client, "You have exited Incognito mode");
						SetClientAndWeaponInvisible(client, activeWeapon, false);
						SetClientSpeedAndGravity(client, 2.0, 0.75);
					}
				}
			}
			else if (buttons & IN_SPEED)
			{
				if (ClientWeaponEqual(client, "weapon_fists") || ClientWeaponEqual(client, "weapon_fists_brass"))
					TeleportToVictim(client);
			}
		}
	}
	return Plugin_Continue;
}

public Action OnWeaponSwitchPost(int client, int weapon)
{
	if (isRoundStart)
	{
		if (IsValidClient(client) && IsValidEdict(weapon))
		{
			if (ClientWeaponEqual(client, "weapon_axe") || ClientWeaponEqual(client, "weapon_machete"))
				SetClientSpeedAndGravity(client, 2.0, 0.75);
			else if (ClientWeaponEqual(client, "weapon_bow"))
			{
				RemovePlayerItem(client, weapon);
				AcceptEntityInput(weapon, "Kill");
				GivePlayerItem(client, "weapon_xbow");
				ClientCommand(client, "use weapon_xbow");
			}
			else
			{
				SetClientSpeedAndGravity(client, 1.0, 0.0);
				if (isClientInvisible[client])
				{
					isClientInvisible[client] = false;
					PrintCenterText(client, "You have exited Incognito mode");
					SetClientAndWeaponInvisible(client, weapon, false);
				}
			}
		}
	}
	return Plugin_Continue;
}

public Action OnTakeDamage(int victim, int &attacker, int &inflictor, float &damage, int &damagetype)
{
	if (isRoundStart)
	{
		if (IsValidClient(attacker))
		{
			if (!IsKickOrFallDown(damagetype))
			{
				if (ClientWeaponEqual(attacker, "weapon_volcanic"))
					damage = 150.0;
				else if(ClientWeaponEqual(attacker, "weapon_sawedoff_shotgun"))
				{
					damage /= 2;
					if (IsValidClient(victim))
						KnockBack(victim, attacker);
				}
				else if (ClientWeaponEqual(attacker, "weapon_axe"))
					damage = GetRandomFloat(10.0, 16.0);
				else if (ClientWeaponEqual(attacker, "weapon_machete"))
					damage = GetRandomFloat(15.0, 21.0);
				else if (ClientWeaponEqual(attacker, "weapon_walker"))
					damage = GetRandomFloat(1.0, 101.0);
				else if (ClientWeaponEqual(attacker, "weapon_coltnavy"))
				{
					damage /= 2;
					if (IsValidClient(victim))
						GetRandomInt(0, 2) == 0 ? ShockVictim(victim) : FreezeVictim(victim);
				}
				return Plugin_Changed;
			}
			else if (IsKick(damagetype))
				if (IsValidClient(victim))
					KnockToSky(victim);
		}
	}
	return Plugin_Continue;
}

public Action Timer_RemoveTeslaEntity(Handle timer, any point)
{
	if(IsValidEdict(point))
		AcceptEntityInput(point, "Kill");
	return Plugin_Handled;
}

public Action Timer_UnfreezeVictim(Handle timer, any victim)
{
	if (IsValidClient(victim))
	{
		if (GetEntityMoveType(victim) == MOVETYPE_NONE)
		{
			PrintCenterText(victim, "You are unfrozen");
			SetEntityMoveType(victim, MOVETYPE_WALK);
		}
	}
	return Plugin_Handled;
}

public Action DataTimer_RemoveNuclearWeapon(Handle timer, Handle datapack)
{
	ResetPack(datapack);
	int client = ReadPackCell(datapack);
	int nuclearWeapon = ReadPackCell(datapack);
	if (IsValidClient(client))
	{
		int activeWeapon = GetEntPropEnt(client, Prop_Send, "m_hActiveWeapon");
		if (activeWeapon == nuclearWeapon)
		{
			RemovePlayerItem(client, activeWeapon);
			AcceptEntityInput(activeWeapon, "Kill");
			ClientCommand(client, "use weapon_fists");
		}
	}
	return Plugin_Handled;
}

bool IsKick(int damagetype)
{
	return damagetype == 268435456;
}

bool IsKickOrFallDown(int damagetype)
{
	return IsKick(damagetype) || damagetype & DMG_FALL;
}

void TeleportToVictim(int attacker)
{
	int victim = GetClientAimTarget(attacker, true);
	if (IsValidClient(attacker) && victim != -1 && victim != -2)
	{
		float victimOrigin[3];
		float attackerAngles[3];
		GetClientAbsOrigin(victim, victimOrigin);
		GetClientEyeAngles(attacker, attackerAngles);
		victimOrigin[0] = (victimOrigin[0] - (48 * ((Cosine(DegToRad(attackerAngles[1]))) * (Cosine(DegToRad(attackerAngles[0]))))));
		victimOrigin[1] = (victimOrigin[1] - (48 * ((Sine(DegToRad(attackerAngles[1]))) * (Cosine(DegToRad(attackerAngles[0]))))));
		SetEntPropVector(attacker, Prop_Data, "m_vecOrigin", victimOrigin);
	}
}

void ShockVictim(int victim)
{
	int point = Entity_Create("point_tesla");
	if (IsValidClient(victim) && point != INVALID_ENT_REFERENCE)
	{
		new Float:victimOrigin[3];
		GetClientAbsOrigin(victim, victimOrigin);
		victimOrigin[2] += 42.0;
		DispatchPointTesla(point);
		Entity_SetAbsOrigin(point, victimOrigin);
		AcceptEntityInput(point, "TurnOn");
		AcceptEntityInput(point, "DoSpark");
		PrintCenterText(victim, "You received an electric shock");
		CreateTimer(0.5, Timer_RemoveTeslaEntity, point);
	}	
}

void FreezeVictim(int victim)
{
	if (IsValidClient(victim))
	{
		if (GetEntityMoveType(victim) == MOVETYPE_WALK)
		{
			EmitSoundToClient(victim, "physics/glass/glass_bottle_break1.wav");
			PrintCenterText(victim, "You are frozen");
			SetEntityMoveType(victim, MOVETYPE_NONE);
			CreateTimer(5.0, Timer_UnfreezeVictim, victim);
		}
	}
}

void KnockBack(int victim, int attacker)
{
	if (IsValidClient(victim) && IsValidClient(attacker))
	{
		float push[3];
		float attackerOrigin[3];
		float victimOrigin[3];
		float victimVelocity[3];
		GetEntPropVector(victim, Prop_Data, "m_vecBaseVelocity", victimVelocity);
		GetClientAbsOrigin(attacker, attackerOrigin);
		GetClientAbsOrigin(victim, victimOrigin);
		MakeVectorFromPoints(attackerOrigin, victimOrigin, push);
		NormalizeVector(push, push);
		ScaleVector(push, 2000.0);
		AddVectors(push, victimVelocity, victimVelocity);
		victimVelocity[2] = 0.0;
		SetEntPropVector(victim, Prop_Data, "m_vecBaseVelocity", victimVelocity);
	}
}

void KnockToSky(int victim)
{
	if (IsValidClient(victim))
	{
		float victimVelocity[3];
		victimVelocity[0] = 0.0;
		victimVelocity[1] = 0.0;
		victimVelocity[2] = 1000.0;
		SetEntPropVector(victim, Prop_Data, "m_vecBaseVelocity", victimVelocity);
	}
}

void ThrowProp(int client, const char[] radius = "-1", const char[] damage = "150")
{
	if (IsValidClient(client))
	{
		float vecOrigin[3];
		float vecAngles[3];
		float vecDirection[3];
		GetClientEyeAngles(client, vecAngles);
		GetClientEyePosition(client, vecOrigin);
		GetAngleVectors(vecAngles, vecDirection, NULL_VECTOR, NULL_VECTOR);
		vecOrigin[0] = (vecOrigin[0] + (100 * ((Cosine(DegToRad(vecAngles[1]))) * (Cosine(DegToRad(vecAngles[0]))))));
		vecOrigin[1] = (vecOrigin[1] + (100 * ((Sine(DegToRad(vecAngles[1]))) * (Cosine(DegToRad(vecAngles[0]))))));
		vecAngles[0] -= (2 * vecAngles[0]);
		vecOrigin[2] = (vecOrigin[2] + (100 * (Sine(DegToRad(vecAngles[0])))));
		NormalizeVector(vecDirection, vecDirection);
		ScaleVector(vecDirection, 2000.0);
		int prop = Entity_Create("prop_physics_override");
		if (prop != INVALID_ENT_REFERENCE)
		{
			DispatchPropPhysicsOverride(prop, radius, damage);
			Entity_SetAbsOrigin(prop, vecOrigin);
			Entity_SetAbsVelocity(prop, vecDirection);
		}
	}
}

void SpawnProp(int client, const char[] radius = "-1", const char[] damage = "150")
{
	if (IsValidClient(client))
	{
		float vecOrigin[3];
		float vecAngles[3];
		GetClientEyeAngles(client, vecAngles);
		GetClientEyePosition(client, vecOrigin);
		TR_TraceRayFilter(vecOrigin, vecAngles, MASK_SOLID, RayType_Infinite, TraceEntityFilterPlayer, client);
		int prop = Entity_Create("prop_physics_override");
		if (prop != INVALID_ENT_REFERENCE)
		{
			TR_GetEndPosition(vecOrigin, INVALID_HANDLE);
			DispatchPropPhysicsOverride(prop, radius, damage);
			Entity_SetAbsOrigin(prop, vecOrigin);
		}
	}
}

void DispatchPropPhysicsOverride(int prop, const char[] radius, const char[] damage)
{
	if (IsValidEdict(prop))
	{
		DispatchKeyValue(prop, "spawnflags", "528");
		DispatchKeyValue(prop, "model", "models/elpaso/barrel1_explosive.mdl");
		DispatchKeyValue(prop, "health", "1");
		if (!StrEqual(radius, "-1"))
			DispatchKeyValue(prop, "ExplodeRadius", radius);
		DispatchKeyValue(prop, "ExplodeDamage", damage);
		DispatchSpawn(prop);
	}
}

void DispatchPointTesla(int point)
{
	if (IsValidEdict(point))
		DispatchKeyValue(point, "m_flRadius", "50");
}

void SetClientAndWeaponInvisible(int client, int weapon, bool invisible)
{
	if (IsValidClient(client) && IsValidEdict(weapon))
	{
		if (invisible)
		{
			SetEntityRenderMode(client, RENDER_NONE);
			SetEntityRenderMode(weapon, RENDER_TRANSCOLOR);
			SetEntityRenderColor(weapon, 0, 0, 0, 0);
		}
		else
		{
			SetEntityRenderMode(client, RENDER_NORMAL);
			SetEntityRenderMode(weapon, RENDER_TRANSCOLOR);
			SetEntityRenderColor(weapon, 255, 255, 255, 255);
		}
	}
}

void SetClientSpeedAndGravity(int client, float speed, float gravity)
{
	if (IsValidClient(client))
	{
		float laggedMovementValue = GetEntPropFloat(client, Prop_Send, "m_flLaggedMovementValue");
		float clientGravityAmount = GetEntityGravity(client);
		if (laggedMovementValue != speed)
			SetEntPropFloat(client, Prop_Send, "m_flLaggedMovementValue", speed);
		if (clientGravityAmount != gravity)
			SetEntityGravity(client, gravity);
	}
}

bool IsValidClient(int client)
{
	return client > 0 && client <= MaxClients;
}

bool TraceEntityFilterPlayer(int entity, int contentsMask)
{
	return entity > MaxClients || !entity;
}

bool ClientWeaponEqual(int client, const char[] weapon)
{
	if (IsValidClient(client))
	{
		char strWeapon[32];
		GetClientWeapon(client, strWeapon, sizeof(strWeapon));
		if (StrEqual(strWeapon, weapon))
			return true;
	}
	return false;
}