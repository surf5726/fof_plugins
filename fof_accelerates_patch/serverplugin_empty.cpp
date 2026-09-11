//========= Copyright Valve Corporation, All rights reserved. ============//
// Replaces FoF's movement acceleration arguments on the 32-bit server.

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <Windows.h>
#include "eiface.h"
#else
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>

// Keep the Linux plugin buildable without an SDK checkout. This is the
// published IServerPluginCallbacks003 ABI; callback order must stay intact.
struct Vector { float x, y, z; };
struct edict_t;
class CCommand;
using CreateInterfaceFn = void *(*)(const char *, int *);
using QueryCvarCookie_t = int;
enum { IFACE_OK = 0, IFACE_FAILED };
enum PLUGIN_RESULT { PLUGIN_CONTINUE = 0, PLUGIN_OVERRIDE, PLUGIN_STOP };
enum EQueryCvarValueStatus
{
	eQueryCvarValueStatus_ValueIntact = 0,
	eQueryCvarValueStatus_CvarNotFound,
	eQueryCvarValueStatus_NotACvar,
	eQueryCvarValueStatus_CvarProtected
};
#define INTERFACEVERSION_ISERVERPLUGINCALLBACKS "ISERVERPLUGINCALLBACKS003"
class IServerPluginCallbacks
{
public:
	virtual bool Load(CreateInterfaceFn, CreateInterfaceFn) = 0;
	virtual void Unload() = 0;
	virtual void Pause() = 0;
	virtual void UnPause() = 0;
	virtual const char *GetPluginDescription() = 0;
	virtual void LevelInit(const char *) = 0;
	virtual void ServerActivate(edict_t *, int, int) = 0;
	virtual void GameFrame(bool) = 0;
	virtual void LevelShutdown() = 0;
	virtual void ClientActive(edict_t *) = 0;
	virtual void ClientDisconnect(edict_t *) = 0;
	virtual void ClientPutInServer(edict_t *, const char *) = 0;
	virtual void SetCommandClient(int) = 0;
	virtual void ClientSettingsChanged(edict_t *) = 0;
	virtual PLUGIN_RESULT ClientConnect(bool *, edict_t *, const char *, const char *, char *, int) = 0;
	virtual PLUGIN_RESULT ClientCommand(edict_t *, const CCommand &) = 0;
	virtual PLUGIN_RESULT NetworkIDValidated(const char *, const char *) = 0;
	virtual void OnQueryCvarValueFinished(QueryCvarCookie_t, edict_t *, EQueryCvarValueStatus, const char *, const char *) = 0;
	virtual void OnEdictAllocated(edict_t *) = 0;
	virtual void OnEdictFreed(const edict_t *) = 0;
};
#endif

#ifdef _WIN32
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
#endif

static_assert(sizeof(void *) == 4, "FoF movement vtable indices require a 32-bit build.");

namespace
{
#ifdef _WIN32
const size_t kAirAccelerateIndex = 20;
const size_t kAccelerateIndex = 24;
// __fastcall receives the original thiscall ECX and reserves EDX.
using AccelerateFn = void (__fastcall *)(void *, void *, Vector &, float, float);
#else
// GCC adds one destructor entry before the movement methods.
const size_t kAirAccelerateIndex = 21;
const size_t kAccelerateIndex = 25;
using AccelerateFn = void (*)(void *, Vector &, float, float);
#endif
const float kAirAcceleration = 100.0f;
const float kGroundAcceleration = 10.0f;

uintptr_t *g_vtable = nullptr;
AccelerateFn g_airAccelerate = nullptr;
AccelerateFn g_accelerate = nullptr;
bool g_paused = true;
#ifdef _WIN32
HMODULE g_moduleKeepalive = nullptr;
#else
void *g_moduleKeepalive = nullptr;
#endif

#ifndef _WIN32
bool GetProtection(const void *address, int &protection)
{
	FILE *maps = std::fopen("/proc/self/maps", "r");
	if (!maps)
		return false;
	char line[512];
	bool found = false;
	const uintptr_t value = reinterpret_cast<uintptr_t>(address);
	while (std::fgets(line, sizeof(line), maps))
	{
		unsigned long long start, end;
		char flags[5];
		if (std::sscanf(line, "%llx-%llx %4s", &start, &end, flags) == 3 &&
			value >= start && value < end)
		{
			protection = (flags[0] == 'r' ? PROT_READ : 0) |
				(flags[1] == 'w' ? PROT_WRITE : 0) | (flags[2] == 'x' ? PROT_EXEC : 0);
			found = flags[0] == 'r';
			break;
		}
	}
	std::fclose(maps);
	return found;
}
#endif

bool WriteVtableEntry(uintptr_t *entry, uintptr_t value)
{
	// An aligned pointer cannot straddle a protection-page boundary.
	if (!entry || reinterpret_cast<uintptr_t>(entry) % sizeof(*entry) != 0)
		return false;
	const uintptr_t previous = *entry;
	if (previous == value)
		return true;
#ifdef _WIN32
	MEMORY_BASIC_INFORMATION region;
	if (!VirtualQuery(entry, &region, sizeof(region)) ||
		region.State != MEM_COMMIT || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		return false;
	const DWORD writable = (region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
		PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
	DWORD protection;
	if (!VirtualProtect(entry, sizeof(*entry), writable, &protection))
		return false;
	*entry = value;
	DWORD ignored;
	if (VirtualProtect(entry, sizeof(*entry), protection, &ignored))
		return true;
	*entry = previous;
	if (!VirtualProtect(entry, sizeof(*entry), protection, &ignored))
		std::fprintf(stderr, "[FoF accelerates] Could not restore vtable-page protection.\n");
#else
	const long pageSize = sysconf(_SC_PAGESIZE);
	int protection;
	if (pageSize <= 0 || !GetProtection(entry, protection))
		return false;
	const uintptr_t address = reinterpret_cast<uintptr_t>(entry);
	void *page = reinterpret_cast<void *>(address - address % pageSize);
	if (mprotect(page, static_cast<size_t>(pageSize), protection | PROT_WRITE) != 0)
		return false;
	*entry = value;
	if (mprotect(page, static_cast<size_t>(pageSize), protection) == 0)
		return true;
	*entry = previous;
	if (mprotect(page, static_cast<size_t>(pageSize), protection) != 0)
		std::fprintf(stderr, "[FoF accelerates] Could not restore vtable-page protection.\n");
#endif
	// The pointer was rolled back while the page was still writable.
	return false;
}

#ifdef _WIN32
void __fastcall AirAccelerate(void *self, void *edx, Vector &direction, float speed, float acceleration)
{
	g_airAccelerate(self, edx, direction, speed, g_paused ? acceleration : kAirAcceleration);
}
void __fastcall Accelerate(void *self, void *edx, Vector &direction, float speed, float acceleration)
{
	g_accelerate(self, edx, direction, speed, g_paused ? acceleration : kGroundAcceleration);
}
#else
void AirAccelerate(void *self, Vector &direction, float speed, float acceleration)
{
	g_airAccelerate(self, direction, speed, g_paused ? acceleration : kAirAcceleration);
}
void Accelerate(void *self, Vector &direction, float speed, float acceleration)
{
	g_accelerate(self, direction, speed, g_paused ? acceleration : kGroundAcceleration);
}
#endif

// Unload has no failure return in the engine ABI. If another plugin chains
// our callback, keep this module resident so that callback remains callable.
bool RetainModule()
{
	if (g_moduleKeepalive)
		return true;
#ifdef _WIN32
	return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		reinterpret_cast<LPCSTR>(AirAccelerate), &g_moduleKeepalive) != FALSE;
#else
	Dl_info info;
	if (!dladdr(reinterpret_cast<void *>(AirAccelerate), &info) || !info.dli_fname)
		return false;
	g_moduleKeepalive = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
	return g_moduleKeepalive != nullptr;
#endif
}

void ReleaseModule()
{
	if (!g_moduleKeepalive)
		return;
#ifdef _WIN32
	FreeLibrary(g_moduleKeepalive);
#else
	dlclose(g_moduleKeepalive);
#endif
	g_moduleKeepalive = nullptr;
}

bool SetHook(size_t index, AccelerateFn hook, AccelerateFn original, bool enabled)
{
	uintptr_t *entry = g_vtable + index;
	const uintptr_t expected = reinterpret_cast<uintptr_t>(enabled ? original : hook);
	const uintptr_t desired = reinterpret_cast<uintptr_t>(enabled ? hook : original);
	if (*entry == desired)
		return true;
	if (*entry != expected)
	{
		std::fprintf(stderr, "[FoF accelerates] Movement slot %zu changed by another hook.\n", index);
		return false;
	}
	return WriteVtableEntry(entry, desired);
}

bool RestoreHooks()
{
	if (!g_vtable)
		return true;
	// Attempt both slots even when one restoration fails.
	const bool ground = SetHook(kAccelerateIndex, Accelerate, g_accelerate, false);
	const bool air = SetHook(kAirAccelerateIndex, AirAccelerate, g_airAccelerate, false);
	return ground && air;
}

bool InstallHooks()
{
	g_paused = true;
	if (SetHook(kAirAccelerateIndex, AirAccelerate, g_airAccelerate, true) &&
		SetHook(kAccelerateIndex, Accelerate, g_accelerate, true))
	{
		g_paused = false;
		return true;
	}
	std::fprintf(stderr, "[FoF accelerates] Could not install both movement hooks.\n");
	return false;
}
}

class CFoFAcceleratesPlugin : public IServerPluginCallbacks
{
public:
	bool Load(CreateInterfaceFn, CreateInterfaceFn factory) override
	{
		if (g_vtable)
			return true; // Do not save our own hooks as the original methods.
		if (!factory)
			return false;
		void *movement = factory("GameMovement001", nullptr);
		if (!movement)
			return false;
		uintptr_t *table = *static_cast<uintptr_t **>(movement);
		if (!table || !table[kAirAccelerateIndex] || !table[kAccelerateIndex])
			return false;
		if (table[kAirAccelerateIndex] == reinterpret_cast<uintptr_t>(AirAccelerate) ||
			table[kAccelerateIndex] == reinterpret_cast<uintptr_t>(Accelerate))
			return false;

		g_vtable = table;
		// Save both originals before exposing either callback to the engine.
		g_airAccelerate = reinterpret_cast<AccelerateFn>(table[kAirAccelerateIndex]);
		g_accelerate = reinterpret_cast<AccelerateFn>(table[kAccelerateIndex]);
		if (InstallHooks())
			return true;
		if (RestoreHooks())
		{
			g_vtable = nullptr;
			g_airAccelerate = nullptr;
			g_accelerate = nullptr;
			return false;
		}
		// Load failure would unload this DLL with a live callback still in it.
		std::fprintf(stderr, "[FoF accelerates] Rollback failed; staying loaded in pass-through mode.\n");
		return true;
	}

	void Unload() override
	{
		g_paused = true;
		if (!RestoreHooks())
		{
			if (RetainModule())
				std::fprintf(stderr, "[FoF accelerates] Unhook failed; retaining module in pass-through mode.\n");
			else
				std::fprintf(stderr, "[FoF accelerates] Unhook and module retention failed; server restart required.\n");
			return;
		}
		g_vtable = nullptr;
		g_airAccelerate = nullptr;
		g_accelerate = nullptr;
		ReleaseModule();
	}
	void Pause() override { g_paused = true; }
	void UnPause() override { if (g_vtable) InstallHooks(); }
	const char *GetPluginDescription() override { return "[FoF] Airaccelerates Patch"; }
	void LevelInit(const char *) override {}
	void ServerActivate(edict_t *, int, int) override {}
	void GameFrame(bool) override {}
	void LevelShutdown() override {}
	void ClientActive(edict_t *) override {}
	void ClientDisconnect(edict_t *) override {}
	void ClientPutInServer(edict_t *, const char *) override {}
	void SetCommandClient(int) override {}
	void ClientSettingsChanged(edict_t *) override {}
	PLUGIN_RESULT ClientConnect(bool *, edict_t *, const char *, const char *, char *, int) override { return PLUGIN_CONTINUE; }
	PLUGIN_RESULT ClientCommand(edict_t *, const CCommand &) override { return PLUGIN_CONTINUE; }
	PLUGIN_RESULT NetworkIDValidated(const char *, const char *) override { return PLUGIN_CONTINUE; }
	void OnQueryCvarValueFinished(QueryCvarCookie_t, edict_t *, EQueryCvarValueStatus, const char *, const char *) override {}
	void OnEdictAllocated(edict_t *) override {}
	void OnEdictFreed(const edict_t *) override {}
};

CFoFAcceleratesPlugin g_FoFAcceleratesPlugin;
#ifdef _WIN32
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CFoFAcceleratesPlugin, IServerPluginCallbacks,
	INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_FoFAcceleratesPlugin);
#else
extern "C" __attribute__((visibility("default"))) void *CreateInterface(const char *name, int *result)
{
	if (name && std::strcmp(name, INTERFACEVERSION_ISERVERPLUGINCALLBACKS) == 0)
	{
		if (result)
			*result = IFACE_OK;
		return static_cast<IServerPluginCallbacks *>(&g_FoFAcceleratesPlugin);
	}
	if (result)
		*result = IFACE_FAILED;
	return nullptr;
}
#endif
