//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//

#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#ifdef _WIN32
#include <Windows.h>
#else
#include <link.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <stddef.h>
#include <string.h>

#include "eiface.h"
#include "igameevents.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

unsigned char *FindPattern(unsigned char *base, size_t size, const unsigned char *pattern, const char *mask)
{
	const size_t patternLength = strlen(mask);
	if (!base || !pattern || !mask || patternLength == 0 || size < patternLength)
		return NULL;

	for (size_t i = 0; i <= size - patternLength; ++i)
	{
		bool found = true;
		for (size_t j = 0; j < patternLength; ++j)
		{
			if (mask[j] != '?' && base[i + j] != pattern[j])
			{
				found = false;
				break;
			}
		}

		if (found)
			return base + i;
	}

	return NULL;
}

#ifdef _WIN32
bool PatchBunnyhopCheck()
{
	static const unsigned char pattern[] =
	{
		0x83, 0xEC, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x33, 0xC4,
		0x89, 0x44, 0x24, 0x00, 0x53, 0x8B, 0xD9, 0x8B, 0x4B
	};
	static const char mask[] = "xx?x????xxxxx?xxxxx";
	static const size_t patchOffset = 0x4EA;

	HMODULE module = GetModuleHandleA("server.dll");
	if (!module)
		return false;

	unsigned char *moduleBase = reinterpret_cast<unsigned char *>(module);
	PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(moduleBase);
	if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
		return false;

	PIMAGE_NT_HEADERS ntHeaders =
		reinterpret_cast<PIMAGE_NT_HEADERS>(moduleBase + dosHeader->e_lfanew);
	if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
		return false;

	const size_t moduleSize = ntHeaders->OptionalHeader.SizeOfImage;
	unsigned char *match = NULL;
	PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
	for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++section)
	{
		if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) ||
			section->VirtualAddress >= moduleSize)
		{
			continue;
		}

		size_t sectionSize = static_cast<size_t>(section->Misc.VirtualSize);
		const size_t remainingSize = moduleSize - section->VirtualAddress;
		if (sectionSize > remainingSize)
			sectionSize = remainingSize;

		match = FindPattern(moduleBase + section->VirtualAddress, sectionSize, pattern, mask);
		if (match)
			break;
	}

	if (!match || static_cast<size_t>(match - moduleBase) + patchOffset + 1 >= moduleSize)
		return false;

	unsigned char *patchAddress = match + patchOffset;
	if (patchAddress[0] != 0x75 || patchAddress[1] != 0x49)
		return false;

	DWORD oldProtect;
	if (!VirtualProtect(patchAddress, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
		return false;

	*patchAddress = 0x74;

	DWORD ignoredProtect;
	const bool restored = VirtualProtect(patchAddress, 1, oldProtect, &ignoredProtect) != FALSE;
	FlushInstructionCache(GetCurrentProcess(), patchAddress, 1);
	return restored;
}
#else
struct PatternSearchContext
{
	const unsigned char *pattern;
	const char *mask;
	size_t patchOffset;
	unsigned char *patchAddress;
	int protection;
};

bool IsServerModule(const char *path)
{
	if (!path || !path[0])
		return false;

	const char *fileName = strrchr(path, '/');
	fileName = fileName ? fileName + 1 : path;
	return strcmp(fileName, "server_srv.so") == 0 || strcmp(fileName, "server.so") == 0;
}

int FindPatternInServerModule(struct dl_phdr_info *info, size_t, void *data)
{
	if (!IsServerModule(info->dlpi_name))
		return 0;

	PatternSearchContext *context = static_cast<PatternSearchContext *>(data);
	for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i)
	{
		const ElfW(Phdr) &header = info->dlpi_phdr[i];
		if (header.p_type != PT_LOAD || !(header.p_flags & PF_X))
			continue;

		unsigned char *segmentBase =
			reinterpret_cast<unsigned char *>(info->dlpi_addr + header.p_vaddr);
		const size_t segmentSize = static_cast<size_t>(header.p_memsz);
		unsigned char *match =
			FindPattern(segmentBase, segmentSize, context->pattern, context->mask);
		if (!match || static_cast<size_t>(match - segmentBase) + context->patchOffset + 1 >= segmentSize)
			continue;

		context->patchAddress = match + context->patchOffset;
		context->protection = 0;
		if (header.p_flags & PF_R)
			context->protection |= PROT_READ;
		if (header.p_flags & PF_W)
			context->protection |= PROT_WRITE;
		if (header.p_flags & PF_X)
			context->protection |= PROT_EXEC;
		return 1;
	}

	return 0;
}

bool PatchBunnyhopCheck()
{
	static const unsigned char pattern[] =
	{
		0x55, 0x89, 0xE5, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x89, 0x5D, 0x00,
		0x8B, 0x5D, 0x00, 0x89, 0x75, 0x00, 0x89, 0x7D, 0x00, 0x8B, 0x73
	};
	static const char mask[] = "xxxxx????xx?xx?xx?xx?xx";

	PatternSearchContext context =
	{
		pattern,
		mask,
		0x8E5,
		NULL,
		0
	};
	dl_iterate_phdr(FindPatternInServerModule, &context);
	if (!context.patchAddress)
		return false;
	if (context.patchAddress[0] != 0x0F || context.patchAddress[1] != 0x84)
		return false;

	const long pageSize = sysconf(_SC_PAGESIZE);
	if (pageSize <= 0)
		return false;

	unsigned char *opcodeAddress = context.patchAddress + 1;
	const uintptr_t address = reinterpret_cast<uintptr_t>(opcodeAddress);
	void *page = reinterpret_cast<void *>(address - address % static_cast<uintptr_t>(pageSize));
	if (mprotect(page, static_cast<size_t>(pageSize), context.protection | PROT_WRITE) != 0)
		return false;

	*opcodeAddress = 0x85;
	__builtin___clear_cache(
		reinterpret_cast<char *>(opcodeAddress),
		reinterpret_cast<char *>(opcodeAddress + 1));

	return mprotect(page, static_cast<size_t>(pageSize), context.protection) == 0;
}
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
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CEmptyServerPlugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_EmtpyServerPlugin);

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

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is loaded, load the interface we need from the engine
//---------------------------------------------------------------------------------
bool CEmptyServerPlugin::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
	return PatchBunnyhopCheck();
}

//---------------------------------------------------------------------------------
// Purpose: called when the plugin is unloaded (turned off)
//---------------------------------------------------------------------------------
void CEmptyServerPlugin::Unload(void)
{
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
	return "[FoF] Bunnyhop Enabler";
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
