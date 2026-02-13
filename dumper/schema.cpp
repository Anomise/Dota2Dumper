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
            bool hasName = SEH_IsReadable((void*)sn, 16) && SEH_ValidateString(sn, 128);
            printf("    vfunc %2d + \"%s\" -> 0x%p", idx, tryModules[mn], scope);
            if (hasName) printf(" (name: %.32s)", sn);
            printf("\n");
        }
    }

    printf("\n    Total scope candidates: %d\n\n", numCandidates);

    if (numCandidates == 0) {
        printf("[-] No scope candidates found\n");
        return false;
    }

    printf("[*] Phase 2: Finding FindDeclaredClass that works...\n\n");

    const char* testClasses[] = {
        "C_BaseEntity", "CBaseEntity",
        "CEntityInstance", "C_DOTA_BaseNPC",
        "CDOTA_BaseNPC", "CGameSceneNode",
        "C_BaseModelEntity",
        nullptr
    };

    for (int ci = 0; ci < numCandidates; ci++) {
        void* scope = candidates[ci].scope;
        int scopeVFunc = candidates[ci].scopeVFunc;
        const char* modName = candidates[ci].moduleName;

        for (int classIdx = 1; classIdx <= 40; classIdx++) {
            for (int tc = 0; testClasses[tc]; tc++) {
                for (int nargs = 1; nargs <= 2; nargs++) {
                    void* r = TryFindClass(scope, classIdx, testClasses[tc], nargs);
                    if (!r) continue;

                    int nameOff = -1;
                    int indirectOff = -1;
                    if (!CheckNameInResult(r, testClasses[tc], &nameOff, &indirectOff))
                        continue;

                    printf("    [+] FOUND WORKING COMBINATION:\n");
                    printf("        Scope vfunc:  %d\n", scopeVFunc);
                    printf("        Scope module: %s\n", modName);
                    printf("        Scope ptr:    0x%p\n", scope);
                    printf("        Class vfunc:  %d (%d args)\n", classIdx, nargs);
                    printf("        Test class:   %s\n", testClasses[tc]);
                    printf("        Result ptr:   0x%p\n", r);
                    printf("        Name offset:  0x%X\n", nameOff);
                    if (indirectOff >= 0)
                        printf("        Indirect off: 0x%X\n", indirectOff);
                    printf("\n");

                    g_VFuncIdx.findTypeScopeForModule = scopeVFunc;
                    g_VFuncIdx.findDeclaredClass = classIdx;
                    g_VFuncIdx.findDeclaredClassNArgs = nargs;
                    g_VFuncIdx.classNameOffset = nameOff;
                    g_VFuncIdx.classInfoPtrOffset = indirectOff;
                    g_VFuncIdx.detected = true;

                    int confirmed = 0;
                    for (int v = 0; testClasses[v]; v++) {
                        void* check = TryFindClass(scope, classIdx, testClasses[v], nargs);
                        if (check) {
                            int tmpName = -1, tmpInd = -1;
                            if (CheckNameInResult(check, testClasses[v], &tmpName, &tmpInd))
                                confirmed++;
                        }
                    }
                    printf("        Confirmed %d/%d test classes\n\n", confirmed, 7);

                    if (confirmed >= 2) return true;

                    g_VFuncIdx.detected = false;
                }
            }
        }
    }

    printf("[-] No working combination found\n\n");
    printf("[*] Phase 3: Diagnostic dump...\n\n");

    for (int ci = 0; ci < numCandidates && ci < 3; ci++) {
        void* scope = candidates[ci].scope;
        printf("    Scope candidate %d (vfunc %d, %s):\n",
            ci, candidates[ci].scopeVFunc, candidates[ci].moduleName);
        DumpHex("      Scope", scope, 96);

        for (int classIdx = 1; classIdx <= 20; classIdx++) {
            void* r1 = TryFindClass(scope, classIdx, "C_BaseEntity", 1);
            void* r2 = TryFindClass(scope, classIdx, "C_BaseEntity", 2);
            if (r1) {
                printf("      vfunc %d (1 arg) -> 0x%p\n", classIdx, r1);
                DumpHex("        ", r1, 64);
            }
            if (r2 && r2 != r1) {
                printf("      vfunc %d (2 args) -> 0x%p\n", classIdx, r2);
                DumpHex("        ", r2, 64);
            }
        }
        printf("\n");
    }

    return false;
}

static bool DetectBindingLayout(void* scope, const char* testClass) {
    printf("[*] Detecting binding layout using '%s'...\n", testClass);

    void* raw = nullptr;
    if (g_VFuncIdx.findDeclaredClassNArgs == 2)
        raw = SEH_VCall2(scope, g_VFuncIdx.findDeclaredClass, testClass, nullptr);
    else
        raw = SEH_VCall1(scope, g_VFuncIdx.findDeclaredClass, testClass);

    if (!raw || !SEH_IsReadable(raw, 0x60)) {
        printf("    [-] Raw result not readable\n");
        return false;
    }

    void* binding = raw;
    if (g_VFuncIdx.classInfoPtrOffset >= 0) {
        void* inner = nullptr;
        if (!SEH_ReadPtr(raw, g_VFuncIdx.classInfoPtrOffset, &inner)) return false;
        if (!inner || !SEH_IsReadable(inner, 0x60)) return false;
        binding = inner;
        printf("    Indirect binding @ 0x%p\n", binding);
    }

    DumpHex("Binding", binding, 96);

    int nameOffset = g_VFuncIdx.classNameOffset;

    const char* verifyName = SEH_ReadStr(binding, nameOffset);
    if (!verifyName || strcmp(verifyName, testClass) != 0) {
        printf("    Name mismatch at 0x%X, searching...\n", nameOffset);
        bool found = false;
        for (int no = 0x00; no <= 0x80; no += 0x08) {
            const char* n = SEH_ReadStr(binding, no);
            if (n && strcmp(n, testClass) == 0) {
                nameOffset = no;
                printf("    Found name at 0x%X\n", nameOffset);
                found = true;
                break;
            }
        }
        if (!found) {
            printf("    [-] Cannot find name in binding\n");
            return false;
        }
    }

    printf("    name offset = 0x%X\n", nameOffset);
    g_BindingLayout.name_offset = nameOffset;

    bool foundFields = false;

    for (int fcOff = 0x10; fcOff <= 0x50; fcOff += 0x02) {
        int16_t fc = 0;
        if (!SEH_ReadI16(binding, fcOff, &fc)) continue;
        if (fc <= 0 || fc > 500) continue;

        for (int fOff = ((fcOff + 6) & ~7); fOff <= 0x70; fOff += 0x08) {
            void* fieldsPtr = nullptr;
            if (!SEH_ReadPtr(binding, fOff, &fieldsPtr)) continue;
            if (!fieldsPtr) continue;
            if (!SEH_IsReadable(fieldsPtr, (int)sizeof(SchemaField_t))) continue;

            const char* firstName = SEH_ReadStr(fieldsPtr, 0x00);
            if (!firstName) continue;

            int32_t firstOffset = 0;
            if (!SEH_ReadI32(fieldsPtr, 0x10, &firstOffset)) continue;
            if (firstOffset < 0 || firstOffset > 0x100000) continue;

            g_BindingLayout.field_count_offset = fcOff;
            g_BindingLayout.fields_offset = fOff;
            printf("    field_count offset = 0x%X (count=%d)\n", fcOff, fc);
            printf("    fields offset      = 0x%X\n", fOff);
            printf("    first field: \"%s\" @ 0x%X\n", firstName, firstOffset);
            foundFields = true;
            goto done_fields;
        }
    }

done_fields:
    if (!foundFields) {
        printf("    [-] Could not find fields\n");
        return false;
    }

    g_BindingLayout.size_offset = -1;
    for (int sOff = nameOffset + 0x08; sOff <= 0x40; sOff += 0x04) {
        if (sOff == g_BindingLayout.field_count_offset) continue;
        int32_t sz = 0;
        if (!SEH_ReadI32(binding, sOff, &sz)) continue;
        if (sz > 0 && sz < 0x100000) {
            g_BindingLayout.size_offset = sOff;
            printf("    size offset        = 0x%X (size=0x%X)\n", sOff, sz);
            break;
        }
    }

    g_BindingLayout.parent_offset = -1;
    for (int pOff = 0x00; pOff <= 0x40; pOff += 0x08) {
        if (pOff == nameOffset) continue;
        if (pOff == g_BindingLayout.fields_offset) continue;
        void* parentPtr = nullptr;
        if (!SEH_ReadPtr(binding, pOff, &parentPtr)) continue;
        if (!parentPtr) continue;
        if (parentPtr == binding) continue;
        if (!SEH_IsReadable(parentPtr, 0x20)) continue;
        const char* parentName = SEH_ReadStr(parentPtr, nameOffset);
        if (parentName && SEH_ValidateString(parentName, 256)) {
            g_BindingLayout.parent_offset = pOff;
            printf("    parent offset      = 0x%X (parent=%s)\n", pOff, parentName);
            break;
        }
    }

    g_BindingLayout.detected = true;
    printf("[+] Layout detected!\n\n");
    return true;
}

void* Schema_FindTypeScope(const char* moduleName) {
    if (!g_pSchema || !g_VFuncIdx.detected) return nullptr;
    void* r = SEH_VCall2(g_pSchema, g_VFuncIdx.findTypeScopeForModule, moduleName, nullptr);
    if (!r) r = SEH_VCall1(g_pSchema, g_VFuncIdx.findTypeScopeForModule, moduleName);
    return r;
}

void* Schema_FindClass(void* scope, const char* className) {
    if (!scope || !g_VFuncIdx.detected) return nullptr;
    void* raw = nullptr;
    if (g_VFuncIdx.findDeclaredClassNArgs == 2)
        raw = SEH_VCall2(scope, g_VFuncIdx.findDeclaredClass, className, nullptr);
    else
        raw = SEH_VCall1(scope, g_VFuncIdx.findDeclaredClass, className);
    if (!raw) return nullptr;
    if (g_VFuncIdx.classInfoPtrOffset >= 0) {
        void* inner = nullptr;
        if (!SEH_ReadPtr(raw, g_VFuncIdx.classInfoPtrOffset, &inner)) return nullptr;
        return inner;
    }
    return raw;
}

bool SchemaInit() {
    g_pSchema = GetInterface("schemasystem.dll", "SchemaSystem_001");
    if (!g_pSchema) {
        printf("[-] SchemaSystem_001 not found\n");
        return false;
    }
    printf("[+] SchemaSystem @ 0x%p\n\n", g_pSchema);

    if (!DetectAll(g_pSchema)) {
        printf("\n[-] Could not find working Schema System combination.\n");
        printf("[-] This Dota 2 build may have changed the interface.\n");
        return false;
    }

    void* workingScope = Schema_FindTypeScope("client.dll");
    if (!workingScope) workingScope = Schema_FindTypeScope("!GlobalTypes");
    if (!workingScope) {
        printf("[-] Cannot get working scope after detection\n");
        return false;
    }

    printf("[+] Working scope @ 0x%p\n\n", workingScope);

    const char* layoutClasses[] = {
        "C_BaseEntity", "CBaseEntity", "CEntityInstance",
        "CGameSceneNode", "C_BaseModelEntity",
        "C_DOTA_BaseNPC", "CDOTA_BaseNPC",
        nullptr
    };

    for (int i = 0; layoutClasses[i]; i++) {
        if (DetectBindingLayout(workingScope, layoutClasses[i]))
            break;
    }

    if (!g_BindingLayout.detected) {
        printf("[-] Could not detect binding layout\n");
        return false;
    }

    return true;
}
