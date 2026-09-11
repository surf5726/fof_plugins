//========= Copyright Valve Corporation, All rights reserved. ============//
// Enables FoF's existing bunnyhop branch in the 32-bit server module.

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
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
#include <stdio.h>
#include <string.h>
#include "eiface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static_assert(sizeof(void *) == 4, "FoF patch signatures require a 32-bit build.");

namespace
{
unsigned char *FindPattern(unsigned char *base, size_t size,
	const unsigned char *pattern, const char *mask)
{
	if (!base || !pattern || !mask)
		return NULL;
	const size_t length = strlen(mask);
	if (!length || size < length)
		return NULL;

	size_t anchor = 0;
	while (anchor < length && mask[anchor] == '?')
		++anchor;
	if (anchor == length)
		return base;

	// Skip candidates using the first fixed byte before comparing the mask.
	const size_t last = size - length;
	size_t offset = 0;
	while (offset <= last)
	{
		const unsigned char *hit = static_cast<const unsigned char *>(
			memchr(base + offset + anchor, pattern[anchor], last - offset + 1));
		if (!hit)
			return NULL;
		offset = static_cast<size_t>(hit - base) - anchor;
		size_t i = 0;
		for (; i < length; ++i)
		{
			if (mask[i] != '?' && base[offset + i] != pattern[i])
				break;
		}
		if (i == length)
			return base + offset;
		++offset;
	}
	return NULL;
}

struct PatchSearch
{
	const unsigned char *pattern;
	const char *mask;
	size_t offset;
	unsigned char expected[2];
	size_t opcodeOffset;
	unsigned char replacement;
	unsigned char *address;
	bool ambiguous;
};

void FindPatchInRegion(unsigned char *base, size_t size, PatchSearch &search)
{
	unsigned char *cursor = base;
	size_t remaining = size;
	while (!search.ambiguous)
	{
		unsigned char *match = FindPattern(cursor, remaining, search.pattern, search.mask);
		if (!match)
			return;
		const size_t available = size - static_cast<size_t>(match - base);
		if (available >= 2 && search.offset <= available - 2 &&
			memcmp(match + search.offset, search.expected, 2) == 0)
		{
			unsigned char *address = match + search.offset + search.opcodeOffset;
			if (search.address && search.address != address)
			{
				search.ambiguous = true;
				return;
			}
			search.address = address;
		}
		cursor = match + 1;
		remaining = size - static_cast<size_t>(cursor - base);
	}
}

#ifdef _WIN32
void FindServerPatch(PatchSearch &search)
{
	HMODULE module = GetModuleHandleA("server.dll");
	if (!module)
		return;
	unsigned char *base = reinterpret_cast<unsigned char *>(module);
	const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
		return;
	const IMAGE_NT_HEADERS *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE ||
		nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
		nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
		return;

	const size_t size = nt->OptionalHeader.SizeOfImage;
	const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
	for (WORD i = 0; i < nt->FileHeader.NumberOfSections && !search.ambiguous; ++i, ++section)
	{
		if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) ||
			!(section->Characteristics & IMAGE_SCN_MEM_READ) || section->VirtualAddress >= size)
			continue;
		size_t sectionSize = section->Misc.VirtualSize;
		if (sectionSize > size - section->VirtualAddress)
			sectionSize = size - section->VirtualAddress;
		FindPatchInRegion(base + section->VirtualAddress, sectionSize, search);
	}
}
#else
bool IsServerModule(const char *path)
{
	if (!path || !path[0])
		return false;
	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	return strcmp(name, "server_srv.so") == 0 || strcmp(name, "server.so") == 0;
}

int FindServerSegment(dl_phdr_info *info, size_t, void *data)
{
	if (!IsServerModule(info->dlpi_name))
		return 0;
	PatchSearch &search = *static_cast<PatchSearch *>(data);
	for (ElfW(Half) i = 0; i < info->dlpi_phnum && !search.ambiguous; ++i)
	{
		const ElfW(Phdr) &header = info->dlpi_phdr[i];
		if (header.p_type == PT_LOAD && (header.p_flags & PF_X) && (header.p_flags & PF_R))
		{
			FindPatchInRegion(reinterpret_cast<unsigned char *>(info->dlpi_addr + header.p_vaddr),
				static_cast<size_t>(header.p_memsz), search);
		}
	}
	return search.ambiguous ? 1 : 0;
}

void FindServerPatch(PatchSearch &search)
{
	dl_iterate_phdr(FindServerSegment, &search);
}

// /proc records current permissions, including changes from other plugins.
bool GetProtection(const void *address, int &protection)
{
	FILE *maps = fopen("/proc/self/maps", "r");
	if (!maps)
		return false;
	char line[512];
	bool found = false;
	const uintptr_t value = reinterpret_cast<uintptr_t>(address);
	while (fgets(line, sizeof(line), maps))
	{
		unsigned long long start, end;
		char flags[5];
		if (sscanf(line, "%llx-%llx %4s", &start, &end, flags) == 3 &&
			value >= start && value < end)
		{
			protection = (flags[0] == 'r' ? PROT_READ : 0) |
				(flags[1] == 'w' ? PROT_WRITE : 0) | (flags[2] == 'x' ? PROT_EXEC : 0);
			found = flags[0] == 'r';
			break;
		}
	}
	fclose(maps);
	return found;
}
#endif

bool WriteOpcode(unsigned char *address, unsigned char value)
{
	const unsigned char previous = *address;
#ifdef _WIN32
	DWORD protection;
	if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &protection))
		return false;
	*address = value;
	const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, 1) != FALSE;
	DWORD ignored;
	if (flushed && VirtualProtect(address, 1, protection, &ignored))
		return true;
	// A failed permission restore must not leave an unreported active patch.
	*address = previous;
	FlushInstructionCache(GetCurrentProcess(), address, 1);
	if (!VirtualProtect(address, 1, protection, &ignored))
		fprintf(stderr, "[FoF bhop] Could not restore code-page protection.\n");
#else
	int protection;
	const long pageSize = sysconf(_SC_PAGESIZE);
	if (pageSize <= 0 || !GetProtection(address, protection))
		return false;
	const uintptr_t valueAddress = reinterpret_cast<uintptr_t>(address);
	void *page = reinterpret_cast<void *>(valueAddress - valueAddress % pageSize);
	if (mprotect(page, static_cast<size_t>(pageSize), protection | PROT_WRITE) != 0)
		return false;
	*address = value;
	__builtin___clear_cache(reinterpret_cast<char *>(address), reinterpret_cast<char *>(address + 1));
	if (mprotect(page, static_cast<size_t>(pageSize), protection) == 0)
		return true;
	*address = previous;
	__builtin___clear_cache(reinterpret_cast<char *>(address), reinterpret_cast<char *>(address + 1));
	if (mprotect(page, static_cast<size_t>(pageSize), protection) != 0)
		fprintf(stderr, "[FoF bhop] Could not restore code-page protection.\n");
#endif
	return false;
}

class BunnyhopPatch
{
public:
	BunnyhopPatch() : m_address(NULL), m_original(0), m_replacement(0), m_enabled(false) {}

	bool Load()
	{
		if (m_address)
			return SetEnabled(true);
#ifdef _WIN32
		static const unsigned char pattern[] = {
			0x83, 0xEC, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x33, 0xC4,
			0x89, 0x44, 0x24, 0x00, 0x53, 0x8B, 0xD9, 0x8B, 0x4B };
		static const char mask[] = "xx?x????xxxxx?xxxxx";
		PatchSearch search = { pattern, mask, 0x4EA, { 0x75, 0x49 }, 0, 0x74, NULL, false };
#else
		static const unsigned char pattern[] = {
			0x55, 0x89, 0xE5, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x89, 0x5D, 0x00,
			0x8B, 0x5D, 0x00, 0x89, 0x75, 0x00, 0x89, 0x7D, 0x00, 0x8B, 0x73 };
		static const char mask[] = "xxxxx????xx?xx?xx?xx?xx";
		PatchSearch search = { pattern, mask, 0x8E5, { 0x0F, 0x84 }, 1, 0x85, NULL, false };
#endif
		static_assert(sizeof(pattern) + 1 == sizeof(mask), "Signature/mask length mismatch.");
		FindServerPatch(search);
		if (!search.address || search.ambiguous)
		{
			fprintf(stderr, "[FoF bhop] No unique supported bunnyhop branch found.\n");
			return false;
		}
		m_address = search.address;
		m_original = search.expected[search.opcodeOffset];
		m_replacement = search.replacement;
		if (SetEnabled(true))
			return true;
		m_address = NULL;
		return false;
	}

	bool SetEnabled(bool enabled)
	{
		if (!m_address)
			return !enabled;
		const unsigned char expected = m_enabled ? m_replacement : m_original;
		if (*m_address != expected)
		{
			fprintf(stderr, "[FoF bhop] Branch changed by another patch; leaving it untouched.\n");
			return false;
		}
		if (enabled == m_enabled)
			return true;
		if (!WriteOpcode(m_address, enabled ? m_replacement : m_original))
		{
			fprintf(stderr, "[FoF bhop] Could not update the bunnyhop branch.\n");
			return false;
		}
		m_enabled = enabled;
		return true;
	}

	void Unload()
	{
		if (SetEnabled(false))
			m_address = NULL;
	}

private:
	unsigned char *m_address;
	unsigned char m_original;
	unsigned char m_replacement;
	bool m_enabled;
};
}

class CFoFBunnyhopPlugin : public IServerPluginCallbacks
{
public:
	bool Load(CreateInterfaceFn, CreateInterfaceFn) override { return m_patch.Load(); }
	void Unload() override { m_patch.Unload(); }
	void Pause() override { m_patch.SetEnabled(false); }
	void UnPause() override { m_patch.SetEnabled(true); }
	const char *GetPluginDescription() override { return "[FoF] Bunnyhop Enabler"; }
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

private:
	BunnyhopPatch m_patch;
};

CFoFBunnyhopPlugin g_FoFBunnyhopPlugin;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CFoFBunnyhopPlugin, IServerPluginCallbacks,
	INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_FoFBunnyhopPlugin);
