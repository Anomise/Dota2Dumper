#include "schema.hpp"
#include <cstring>

void*              g_pSchema       = nullptr;
BindingLayout      g_BindingLayout = {};
SchemaVFuncIndices g_VFuncIdx      = {};

void* GetInterface(const char* moduleName, const char* ifaceName) {
    HMODULE hMod = GetModuleHandleA(moduleName);
    if (!hMod) {
        printf("[-] Module not loaded: %s\n", moduleName);
        return nullptr;
    }
    using Fn = void*(*)(const char*, int*);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(hMod, "CreateInterface"));
    if (!fn) {
        printf("[-] CreateInterface missing in %s\n", moduleName);
        return nullptr;
    }
    int rc = 0;
    void* p = fn(ifaceName, &rc);
    if (!p) printf("[-] Interface %s not found\n", ifaceName);
    return p;
}

static void DumpHex(const char* label, void* addr, int size) {
    printf("    %s @ 0x%p:\n    ", label, addr);
    unsigned char* p = (unsigned char*)addr;
    for (int i = 0; i < size; i++) {
        if (!SEH_IsReadable(p + i, 1))
            printf("?? ");
        else
            printf("%02X ", p[i]);
        if ((i + 1) % 16 == 0 && i + 1 < size) printf("\n    ");
    }
    printf("\n");
}

static bool CheckNameInResult(void* r, const char* className, int* outNameOff, int* outIndirectOff) {
    for (int no = 0x00; no <= 0x100; no += 0x08) {
        const char* name = SEH_ReadStr(r, no);
        if (name && strcmp(name, className) == 0) {
            *outNameOff = no;
            *outIndirectOff = -1;
            return true;
        }
    }
    for (int off = 0x00; off <= 0x60; off += 0x08) {
        void* inner = nullptr;
        if (!SEH_ReadPtr(r, off, &inner)) continue;
        if (!inner || inner == r) continue;
        if (!SEH_IsReadable(inner, 0x40)) continue;
        for (int no = 0x00; no <= 0x80; no += 0x08) {
            const char* name = SEH_ReadStr(inner, no);
            if (name && strcmp(name, className) == 0) {
                *outNameOff = no;
                *outIndirectOff = off;
                return true;
            }
        }
    }
    return false;
}

static void* TryGetScope(void* schema, int idx, const char* mod) {
    void* r = SEH_VCall2(schema, idx, mod, nullptr);
    if (r && SEH_IsReadable(r, 0x20)) {
        void* vt = nullptr;
        if (SEH_ReadPtr(r, 0, &vt) && vt && SEH_IsReadable(vt, 0x20))
            return r;
    }
    r = SEH_VCall1(schema, idx, mod);
    if (r && SEH_IsReadable(r, 0x20)) {
        void* vt = nullptr;
        if (SEH_ReadPtr(r, 0, &vt) && vt && SEH_IsReadable(vt, 0x20))
            return r;
    }
    return nullptr;
}

static void* TryFindClass(void* scope, int idx, const char* cls, int nargs) {
    void* r = nullptr;
    if (nargs == 2)
        r = SEH_VCall2(scope, idx, cls, nullptr);
    else
        r = SEH_VCall1(scope, idx, cls);
    if (r && SEH_IsReadable(r, 0x30))
        return r;
    return nullptr;
}

struct ScopeCandidate {
    int scopeVFunc;
    const char* moduleName;
    void* scope;
};

static bool DetectAll(void* schema) {
    printf("[*] Phase 1: Finding all valid TypeScopes...\n\n");

    const char* tryModules[] = {
        "client.dll", "!GlobalTypes", "server.dll", "engine2.dll",
        "client", "server", "schemasystem.dll",
        "worldrenderer.dll", "particles.dll",
        nullptr
    };

    ScopeCandidate candidates[64];
    int numCandidates = 0;

    for (int idx = 8; idx <= 30; idx++) {
        for (int mn = 0; tryModules[mn] && numCandidates < 64; mn++) {
            void* scope = TryGetScope(schema, idx, tryModules[mn]);
            if (!scope) continue;

            bool duplicate = false;
            for (int c = 0; c < numCandidates; c++) {
                if (candidates[c].scope == scope && candidates[c].scopeVFunc == idx) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            candidates[numCandidates].scopeVFunc = idx;
            candidates[numCandidates].moduleName = tryModules[mn];
            candidates[numCandidates].scope = scope;
            numCandidates++;

            const char* sn = (const char*)((uintptr_t)scope + 0x08);
            bool hasName 
