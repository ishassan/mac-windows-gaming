/*
 *  Port change: ADVAPI32 registry functions.
 *
 *  The registry is read-only data of the game (GAME_REGISTRY_VALUES in the
 *  game's game.h): { "key", "value name", type, "data" }, with the key as
 *  "HKLM\\Software\\..." (the Wow6432Node part is left out; keys and names
 *  do not depend on case). REG_DWORD data is a number in the string.
 *  Without GAME_REGISTRY_VALUES no key exists, and each game uses its
 *  defaults. Values that the game writes stay in memory until it exits.
 *
 *  MIT license, see README.md.
 */

#include "game-info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "guest.h"

#ifndef EXTERN_C
#define EXTERN_C extern "C"
#endif

#define ERROR_SUCCESS        0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_MORE_DATA      234
#define ERROR_NO_MORE_ITEMS  259
#define REG_SZ    1
#define REG_DWORD 4

struct reg_init { const char *key, *name; uint32_t type; const char *data; };

#ifdef GAME_REGISTRY_VALUES
static const reg_init init_values[] = { GAME_REGISTRY_VALUES, { NULL, NULL, 0, NULL } };
#else
static const reg_init init_values[] = { { NULL, NULL, 0, NULL } };
#endif

struct reg_value { uint32_t type; std::vector<uint8_t> data; };

static std::mutex reg_lock;
static std::map<std::string, std::map<std::string, reg_value>> keys;   /* lower case */
static std::map<uint32_t, std::string> handles;
static uint32_t next_handle = 0x1000;
static int reg_loaded;

static int trace_reg(void)
{
    static int t = -1;
    if (t < 0)
    {
        const char *v = game_getenv("TRACE_REG");
        t = (v != NULL && *v != 0 && *v != '0');
    }
    return t;
}

static std::string lower(const std::string &s)
{
    std::string r(s);
    for (auto &c : r) c = (char)tolower((unsigned char)c);
    return r;
}

/* "HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\X\" -> "hklm\software\x" */
static std::string norm_key(const std::string &k)
{
    std::string s = lower(k);
    for (auto &c : s) if (c == '/') c = '\\';
    const char *roots[][2] = { { "hkey_local_machine", "hklm" }, { "hkey_current_user", "hkcu" }, { "hkey_classes_root", "hkcr" } };
    for (auto &r : roots)
    {
        size_t n = strlen(r[0]);
        if (s.compare(0, n, r[0]) == 0) s = r[1] + s.substr(n);
    }
    size_t w;
    while ((w = s.find("\\wow6432node")) != std::string::npos) s.erase(w, 12);
    while (s.find("\\\\") != std::string::npos) s.erase(s.find("\\\\"), 1);
    while (!s.empty() && s.back() == '\\') s.pop_back();
    return s;
}

static void load_values(void)
{
    if (reg_loaded) return;
    reg_loaded = 1;
    for (const reg_init *v = init_values; v->key; v++)
    {
        reg_value rv;
        rv.type = v->type;
        if (v->type == REG_DWORD)
        {
            uint32_t d = (uint32_t)strtoul(v->data, NULL, 0);
            rv.data.resize(4);
            memcpy(rv.data.data(), &d, 4);
        }
        else
        {
            rv.data.assign(v->data, v->data + strlen(v->data) + 1);
        }
        keys[norm_key(v->key)][lower(v->name)] = rv;
    }
}

static std::string root_name(uint32_t hKey)
{
    switch (hKey)
    {
        case 0x80000000: return "hkcr";
        case 0x80000001: return "hkcu";
        case 0x80000002: return "hklm";
        default:
        {
            auto it = handles.find(hKey);
            return (it == handles.end()) ? std::string() : it->second;
        }
    }
}

static int key_exists(const std::string &k)
{
    if (keys.count(k)) return 1;
    std::string prefix = k + "\\";
    for (auto &e : keys)
    {
        if (e.first.compare(0, prefix.size(), prefix) == 0) return 1;
    }
    return 0;
}

static uint32_t open_key(uint32_t hKey, const char *sub, uint32_t *phkResult, int create)
{
    std::lock_guard<std::mutex> g(reg_lock);
    load_values();
    std::string base = root_name(hKey);
    std::string k = norm_key(base + "\\" + (sub ? sub : ""));
    int ok = !base.empty() && (create || key_exists(k));
    if (trace_reg()) fprintf(stderr, "RegOpenKey(%s) -> %s\n", k.c_str(), ok ? "ok" : "not found");
    if (!ok)
    {
        if (phkResult) wr32(phkResult, 0);
        return ERROR_FILE_NOT_FOUND;
    }
    if (create) keys[k];
    uint32_t h = next_handle++;
    handles[h] = k;
    if (phkResult) wr32(phkResult, h);
    return ERROR_SUCCESS;
}

EXTERN_C uint32_t RegOpenKeyExA_c(uint32_t hKey, const char *lpSubKey, uint32_t ulOptions, uint32_t samDesired, void *phkResult)
{
    return open_key(hKey, lpSubKey, (uint32_t *)phkResult, 0);
}

EXTERN_C uint32_t RegOpenKeyA_c(uint32_t hKey, const char *lpSubKey, void *phkResult)
{
    return open_key(hKey, lpSubKey, (uint32_t *)phkResult, 0);
}

EXTERN_C uint32_t RegCreateKeyExA_c(uint32_t hKey, const char *lpSubKey, uint32_t Reserved, char *lpClass, uint32_t dwOptions,
                                    uint32_t samDesired, void *lpSecurityAttributes, void *phkResult, void *lpdwDisposition)
{
    if (lpdwDisposition) wr32(lpdwDisposition, 2);   /* REG_OPENED_EXISTING_KEY */
    return open_key(hKey, lpSubKey, (uint32_t *)phkResult, 1);
}

EXTERN_C uint32_t RegCloseKey_c(uint32_t hKey)
{
    std::lock_guard<std::mutex> g(reg_lock);
    handles.erase(hKey);
    return ERROR_SUCCESS;
}

EXTERN_C uint32_t RegQueryValueExA_c(uint32_t hKey, const char *lpValueName, void *lpReserved, void *lpType, void *lpData, void *lpcbData)
{
    std::lock_guard<std::mutex> g(reg_lock);
    load_values();
    std::string k = root_name(hKey);
    auto ki = keys.find(k);
    std::string name = lower(lpValueName ? lpValueName : "");
    if (ki == keys.end() || !ki->second.count(name))
    {
        if (trace_reg()) fprintf(stderr, "RegQueryValueEx(%s, %s) -> not found\n", k.c_str(), name.c_str());
        return ERROR_FILE_NOT_FOUND;
    }
    const reg_value &v = ki->second[name];
    if (trace_reg()) fprintf(stderr, "RegQueryValueEx(%s, %s) -> found\n", k.c_str(), name.c_str());
    if (lpType) wr32(lpType, v.type);
    uint32_t size = (uint32_t)v.data.size();
    if (lpData == NULL)
    {
        if (lpcbData) wr32(lpcbData, size);
        return ERROR_SUCCESS;
    }
    uint32_t have = lpcbData ? rd32(lpcbData) : 0;
    if (lpcbData) wr32(lpcbData, size);
    if (have < size) return ERROR_MORE_DATA;
    memcpy(lpData, v.data.data(), size);
    return ERROR_SUCCESS;
}

EXTERN_C uint32_t RegSetValueExA_c(uint32_t hKey, const char *lpValueName, uint32_t Reserved, uint32_t dwType, const void *lpData, uint32_t cbData)
{
    std::lock_guard<std::mutex> g(reg_lock);
    load_values();
    std::string k = root_name(hKey);
    if (k.empty()) return ERROR_FILE_NOT_FOUND;
    reg_value v;
    v.type = dwType;
    if (lpData) v.data.assign((const uint8_t *)lpData, (const uint8_t *)lpData + cbData);
    keys[k][lower(lpValueName ? lpValueName : "")] = v;
    if (trace_reg()) fprintf(stderr, "RegSetValueEx(%s, %s): kept in memory only\n", k.c_str(), lpValueName ? lpValueName : "");
    return ERROR_SUCCESS;
}

EXTERN_C uint32_t RegEnumValueA_c(uint32_t hKey, uint32_t dwIndex, char *name, void *namelen, void *res, void *type, void *data, void *datalen)
{
    return ERROR_NO_MORE_ITEMS;
}

EXTERN_C uint32_t RegQueryInfoKeyA_c(uint32_t hKey, char *a, void *b, void *c, void *subkeys, void *e, void *f,
                                     void *values, void *h, void *i, void *j, void *k)
{
    if (subkeys) wr32(subkeys, 0);
    if (values) wr32(values, 0);
    return ERROR_FILE_NOT_FOUND;
}
