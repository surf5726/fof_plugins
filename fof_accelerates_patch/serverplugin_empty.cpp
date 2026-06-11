
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#include <Windows.h>
#include <Psapi.h>
#include "eiface.h"
#include "igameevents.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
#elif defined(_LINUX)
#include <cstdio>
#include <unistd.h>
#include <sys/mman.h>

struct Vector
{
	float x;
	float y;
	float z;
};

struct edict_t;
class CCommand;
class KeyValues;

using CreateInterfaceFn = void *(*)(const char *pName, int *pReturnCode);
using QueryCvarCookie_t = int;

enum
{
	IFACE_OK = 0,
	IFACE_FAILED
};

enum PLUGIN_RESULT
{
	PLUGIN_CONTINUE = 0,
	PLUGIN_OVERRIDE,
	PLUGIN_STOP,
};

enum EQueryCvarValueStatus
{
	eQueryCvarValueStatus_ValueIntact = 0,
	eQueryCvarValueStatus_CvarNotFound = 1,
	eQueryCvarValueStatus_NotACvar = 2,
	eQueryCvarValueStatus_CvarProtected = 3,
};

#define INTERFACEVERSION_ISERVERPLUGINCALLBACKS "ISERVERPLUGINCALLBACKS003"

class IGameEventListener
{
public:
	virtual void FireGameEvent(KeyValues *event) = 0;
};

class IServerPluginCallbacks
{
public:
	virtual bool Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory) = 0;
	virtual void Unload(void) = 0;
	virtual void Pause(void) = 0;
	virtual void UnPause(void) = 0;
	virtual const char *GetPluginDescription(void) = 0;
	virtual void LevelInit(char const *pMapName) = 0;
	virtual void ServerActivate(edict_t *pEdictList, int edictCount, int clientMax) = 0;
	virtual void GameFrame(bool simulating) = 0;
	virtual void LevelShutdown(void) = 0;
	virtual void ClientActive(edict_t *pEntity) = 0;
	virtual void ClientDisconnect(edict_t *pEntity) = 0;
	virtual void ClientPutInServer(edict_t *pEntity, char const *playername) = 0;
	virtual void SetCommandClient(int index) = 0;
	virtual void ClientSettingsChanged(edict_t *pEdict) = 0;
	virtual PLUGIN_RESULT ClientConnect(bool *bAllowConnect, edict_t *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen) = 0;
	virtual PLUGIN_RESULT ClientCommand(edict_t *pEntity, const CCommand &args) = 0;
	virtual PLUGIN_RESULT NetworkIDValidated(const char *pszUserName, const char *pszNetworkID) = 0;
	virtual void OnQueryCvarValueFinished(QueryCvarCookie_t iCookie, edict_t *pPlayerEntity, EQueryCvarValueStatus eStatus, const char *pCvarName, const char *pCvarValue) = 0;
	virtual void OnEdictAllocated(edict_t *edict) = 0;
	virtual void OnEdictFreed(const edict_t *edict) = 0;
};
#endif

//---------------------------------------------------------------------------------
// Purpose: a sample 3rd party plugin class
//---------------------------------------------------------------------------------
class CEmptyServerPlugin : public IServerPluginCallbacks, public IGameEventListener
{
public:
	CEmptyServerPlugin();
	~CEmptyServerPlugin();

	// IServerPluginCallbacks methods
	virtual bool			Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory);
	virtual void			Unload(void);
	virtual void			Pause(void);
	virtual void			UnPause(void);
	virtual const char *GetPluginDescription(void);
	virtual void			LevelInit(char const *pMapName);
	virtual void			ServerActivate(edict_t *pEdictList, int edictCount, int clientMax);
	virtual void			GameFrame(bool simulating);
	virtual void			LevelShutdown(void);
	virtual void			ClientActive(edict_t *pEntity);
	virtual void			ClientDisconnect(edict_t *pEntity);
	virtual void			ClientPutInServer(edict_t *pEntity, char const *playername);
	virtual void			SetCommandClient(int index);
	virtual void			ClientSettingsChanged(edict_t *pEdict);
	virtual PLUGIN_RESULT	ClientConnect(bool *bAllowConnect, edict_t *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen);
	virtual PLUGIN_RESULT	ClientCommand(edict_t *pEntity, const CCommand &args);
	virtual PLUGIN_RESULT	NetworkIDValidated(const char *pszUserName, const char *pszNetworkID);
	virtual void			OnQueryCvarValueFinished(QueryCvarCookie_t iCookie, edict_t *pPlayerEntity, EQueryCvarValueStatus eStatus, const char *pCvarName, const char *pCvarValue);
	virtual void			OnEdictAllocated(edict_t *edict);
	virtual void			OnEdictFreed(const edict_t *edict);

	// IGameEventListener Interface
	virtual void FireGameEvent(KeyValues *event);

	virtual int GetCommandIndex() { return m_iClientCommandIndex; }
private:
	int m_iClientCommandIndex;
};

// 
// The plugin is a static singleton that is exported as an interface
//
CEmptyServerPlugin g_EmtpyServerPlugin;
#if defined(_WIN32)
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CEmptyServerPlugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_EmtpyServerPlugin);
#elif defined(_LINUX)
extern "C" __attribute__((visibility("default"))) void *CreateInterface(const char *pName, int *pReturnCode)
{
	if (pName && std::strcmp(pName, INTERFACEVERSION_ISERVERPLUGINCALLBACKS) == 0) {
		if (pReturnCode) {
			*pReturnCode = IFACE_OK;
		}
		return static_cast<IServerPluginCallbacks *>(&g_EmtpyServerPlugin);
	}

	if (pReturnCode) {
		*pReturnCode = IFACE_FAILED;
	}
	return nullptr;
}
#endif

//---------------------------------------------------------------------------------
// Purpose: constructor/destructor
//---------------------------------------------------------------------------------
CEmptyServerPlugin::CEmptyServerPlugin()
{
	m_iClientCommandIndex = 0;
}

CEmptyServerPlugin::~CEmptyServerPlugin()
{
}

static bool WriteVtableEntry(uintptr_t *entry, uintptr_t value)
{
#if defined(_WIN32)
	DWORD oldProtect = 0;
	if (!VirtualProtect(entry, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		return false;
	}

	*entry = value;
	VirtualProtect(entry, sizeof(uintptr_t), oldProtect, &oldProtect);
	return true;
#elif defined(_LINUX)
	const long pageSize = sysconf(_SC_PAGESIZE);
	if (pageSize <= 0) {
		return false;
	}

	const uintptr_t page = reinterpret_cast<uintptr_t>(entry) & ~(static_cast<uintptr_t>(pageSize) - 1);
	if (mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(pageSize), PROT_READ | PROT_WRITE) != 0) {
		return false;
	}

	*entry = value;
	__builtin___clear_cache(reinterpret_cast<char *>(page), reinterpret_cast<char *>(page + pageSize));
	mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(pageSize), PROT_READ);
	return true;
#endif
}

void *HookMethod(void *pObj, void *pHookMethod, size_t index)
{
	if (!pObj || !pHookMethod) {
		return nullptr;
	}

	uintptr_t *vtable = *(uintptr_t **)pObj;
	uintptr_t *entry = &vtable[index];
	uintptr_t original = *entry;
	if (!WriteVtableEntry(entry, reinterpret_cast<uintptr_t>(pHookMethod))) {
		return nullptr;
	}

	return reinterpret_cast<void *>(original);
}

// .rdata:105BBF6C                 dd offset sub_1019D1E0
#if defined(_WIN32)
void(__fastcall *pfnAiraccelerate)(void *pthis, int dummy, Vector &wishdir, float wishspeed, float accel);
void __fastcall myAiraccelerate(void *pthis, int dummy, Vector &wishdir, float wishspeed, float accel)
{
	pfnAiraccelerate(pthis, dummy, wishdir, wishspeed, 100.0f);
}

// .rdata:105BBF7C                 dd offset sub_1019D020
int(__fastcall *pfnAccelerate)(void *pthis, int dummy, Vector &wishdir, float wishspeed, float accel);
int __fastcall myAccelerate(void *pthis, int dummy, Vector &wishdir, float wishspeed, float accel)
{
	return pfnAccelerate(pthis, dummy, wishdir, wishspeed, 10.0f);
}
#elif defined(_LINUX)
void(*pfnAiraccelerate)(void *pthis, Vector &wishdir, float wishspeed, float accel);
void myAiraccelerate(void *pthis, Vector &wishdir, float wishspeed, float accel)
{
	pfnAiraccelerate(pthis, wishdir, wishspeed, 100.0f);
}

// Linux/GCC vtables have one extra destructor slot compared with the Windows build.
int(*pfnAccelerate)(void *pthis, Vector &wishdir, float wishspeed, float accel);
int myAccelerate(void *pthis, Vector &wishdir, float wishspeed, float accel)
{
	return pfnAccelerate(pthis, wishdir, wishspeed, 10.0f);
}
#endif

void *g_pGameMovement;

#if defined(_WIN32)
static constexpr size_t kAirAccelerateIndex = 20;
static constexpr size_t kAccelerateIndex = 24;
#elif defined(_LINUX)
static constexpr size_t kAirAccelerateIndex = 21;
static constexpr size_t kAccelerateIndex = 25;
#endif

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is loaded, load the interface we need from the engine
//---------------------------------------------------------------------------------
bool CEmptyServerPlugin::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
	(void)interfaceFactory;

	g_pGameMovement = gameServerFactory("GameMovement001", nullptr);
	if (!g_pGameMovement) {
		return false;
	}

	pfnAiraccelerate = decltype(pfnAiraccelerate)(HookMethod(g_pGameMovement, reinterpret_cast<void *>(myAiraccelerate), kAirAccelerateIndex));
	pfnAccelerate = decltype(pfnAccelerate)(HookMethod(g_pGameMovement, reinterpret_cast<void *>(myAccelerate), kAccelerateIndex));

	if (!pfnAiraccelerate || !pfnAccelerate) {
		Unload();
		return false;
	}

	return true;
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is unloaded (turned off)
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::Unload(void)
{
	if (g_pGameMovement && pfnAccelerate) {
		HookMethod(g_pGameMovement, reinterpret_cast<void *>(pfnAccelerate), kAccelerateIndex);
		pfnAccelerate = nullptr;
	}

	if (g_pGameMovement && pfnAiraccelerate) {
		HookMethod(g_pGameMovement, reinterpret_cast<void *>(pfnAiraccelerate), kAirAccelerateIndex);
		pfnAiraccelerate = nullptr;
	}

	g_pGameMovement = nullptr;
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is paused (i.e should stop running but isn't unloaded)
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::Pause(void)
{
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is unpaused (i.e should start executing again)
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::UnPause(void)
{
}

//---------------------------------------------------------------------------------
// Purpose: the name of this plugin, returned in "plugin_print" command
//---------------------------------------------------------------------------------
const char *CEmptyServerPlugin::GetPluginDescription(void)
{
	return "[FoF] Airaccelerates Patch";
}

//---------------------------------------------------------------------------------
// Purpose: called on level start
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::LevelInit(char const *pMapName)
{
}

//---------------------------------------------------------------------------------
// Purpose: called on level start, when the server is ready to accept client connections
//		edictCount is the number of entities in the level, clientMax is the max client count
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::ServerActivate(edict_t *pEdictList, int edictCount, int clientMax)
{
}

//---------------------------------------------------------------------------------
// Purpose: called once per server frame, do recurring work here (like checking for timeouts)
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::GameFrame(bool simulating)
{
}

//---------------------------------------------------------------------------------
// Purpose: called on level end (as the server is shutting down or going to a new map)
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::LevelShutdown(void) // !!!!this can get called multiple times per map change
{
}

//---------------------------------------------------------------------------------
// Purpose: called when a client spawns into a server (i.e as they begin to play)
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::ClientActive(edict_t *pEntity)
{
}

//---------------------------------------------------------------------------------
// Purpose: called when a client leaves a server (or is timed out)
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::ClientDisconnect(edict_t *pEntity)
{
}

//---------------------------------------------------------------------------------
// Purpose: called on 
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::ClientPutInServer(edict_t *pEntity, char const *playername)
{
}

//---------------------------------------------------------------------------------
// Purpose: called on level start
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::SetCommandClient(int index)
{
	m_iClientCommandIndex = index;
}

//---------------------------------------------------------------------------------
// Purpose: called on level start
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::ClientSettingsChanged(edict_t *pEdict)
{
}

//---------------------------------------------------------------------------------
// Purpose: called when a client joins a server
//---------------------------------------------------------------------------------
PLUGIN_RESULT CEmptyServerPlugin::ClientConnect(bool *bAllowConnect, edict_t *pEntity, const char *pszName, const char *pszAddress, char *reject, int maxrejectlen)
{
	return PLUGIN_CONTINUE;
}

//---------------------------------------------------------------------------------
// Purpose: called when a client types in a command (only a subset of commands however, not CON_COMMAND's)
//---------------------------------------------------------------------------------
PLUGIN_RESULT CEmptyServerPlugin::ClientCommand(edict_t *pEntity, const CCommand &args)
{
	return PLUGIN_CONTINUE;
}

//---------------------------------------------------------------------------------
// Purpose: called when a client is authenticated
//---------------------------------------------------------------------------------
PLUGIN_RESULT CEmptyServerPlugin::NetworkIDValidated(const char *pszUserName, const char *pszNetworkID)
{
	return PLUGIN_CONTINUE;
}

//---------------------------------------------------------------------------------
// Purpose: called when a cvar value query is finished
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::OnQueryCvarValueFinished(QueryCvarCookie_t iCookie, edict_t *pPlayerEntity, EQueryCvarValueStatus eStatus, const char *pCvarName, const char *pCvarValue)
{
}
void CEmptyServerPlugin::OnEdictAllocated(edict_t *edict)
{
}
void CEmptyServerPlugin::OnEdictFreed(const edict_t *edict)
{
}

//---------------------------------------------------------------------------------
// Purpose: called when an event is fired
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::FireGameEvent(KeyValues *event)
{
}
