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

static const char* ReadAsciiStr(const void* base, int offset) {
    const char* s = SEH_ReadStr(base, offset);
    if (!s) return nullptr;
    if (!SEH_IsAsciiString(s, 256)) return nullptr;
    return s;
}

static bool CheckNameInResult(void* r, const char* className, int* outNameOff, int* outIndirectOff) {
    for (int no = 0x00; no <= 0x100; no += 0x08) {
        const char* name = ReadAsciiStr(r, no);
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
            const char* name = ReadAsciiStr(inner, no);
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

static int CountValidAsciiFields(void* fieldsPtr, int fieldCount, int fieldStride) {
    int valid = 0;
    for (int i = 0; i < fieldCount && i < 50; i++) {
        void* fieldAddr = (void*)((uintptr_t)fieldsPtr + i * fieldStride);
        if (!SEH_IsReadable(fieldAddr, fieldStride)) break;

        const char* fn = ReadAsciiStr(fieldAddr, 0x00);
        if (!fn) continue;

        int32_t foff = 0;
        if (!SEH_ReadI32(fieldAddr, 0x10, &foff)) continue;
        if (foff < 0 || foff > 0x100000) continue;

        valid++;
    }
    return valid;
}

static bool TryBindingLayout(void* binding, const char* testClass, int nameOffset) {
    static const int strides[] = { 0x20, 0x28, 0x30, 0x18, 0x38, 0x40 };

    for (int fcOff = 0x10; fcOff <= 0x50; fcOff += 0x02) {
        int16_t fc = 0;
        if (!SEH_ReadI16(binding, fcOff, &fc)) continue;
        if (fc <= 0 || fc > 500) continue;

        for (int fOff = ((fcOff + 6) & ~7); fOff <= 0x78; fOff += 0x08) {
            void* fieldsPtr = nullptr;
            if (!SEH_ReadPtr(binding, fOff, &fieldsPtr)) continue;
            if (!fieldsPtr) continue;
            if (!SEH_IsReadable(fieldsPtr, 0x20)) continue;

            for (int si = 0; si < 6; si++) {
                int stride = strides[si];
                int validCount = CountValidAsciiFields(fieldsPtr, fc, stride);

                if (validCount >= 3 && validCount >= fc / 2) {
                    g_BindingLayout.name_offset = nameOffset;
                    g_BindingLayout.field_count_offset = fcOff;
                    g_BindingLayout.fields_offset = fOff;

                    printf("    field_count offset = 0x%X (count=%d)\n", fcOff, fc);
                    printf("    fields offset      = 0x%X\n", fOff);
                    printf("    field stride       = 0x%X\n", stride);
                    printf("    valid fields       = %d / %d\n", validCount, fc);

                    void* f0 = (void*)((uintptr_t)fieldsPtr);
                    const char* fn0 = ReadAsciiStr(f0, 0x00);
                    int32_t fo0 = 0;
                    SEH_ReadI32(f0, 0x10, &fo0);
                    if (fn0) printf("    first field: \"%s\" @ 0x%X\n", fn0, fo0);

                    g_BindingLayout.size_offset = -1;
                    for (int sOff = nameOffset + 0x08; sOff <= 0x40; sOff += 0x04) {
                        if (sOff == fcOff) continue;
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
                        if (pOff == fOff) continue;
                        void* pp = nullptr;
                        if (!SEH_ReadPtr(binding, pOff, &pp)) continue;
                        if (!pp || pp == binding) continue;
                        if (!SEH_IsReadable(pp, 0x20)) continue;
                        const char* pname = ReadAsciiStr(pp, nameOffset);
                        if (pname) {
                            g_BindingLayout.parent_offset = pOff;
                            printf("    parent offset      = 0x%X (%s)\n", pOff, pname);
                            break;
                        }
                    }

                    g_BindingLayout.detected = true;
                    return true;
                }
            }
        }
    }

    return false;
}

struct ScopeCandidate {
    int vfunc;
    const char* module;
    void* scope;
};

static bool DetectAll(void* schema) {
    printf("[*] Phase 1: Finding TypeScopes...\n\n");

    const char* tryModules[] = {
        "client.dll", "!GlobalTypes", "server.dll", "engine2.dll",
        "client", "server", "schemasystem.dll",
        "worldrenderer.dll", "particles.dll",
        "animationsystem.dll", "pulse_system.dll",
        "materialsystem2.dll",
        nullptr
    };

    ScopeCandidate candidates[128];
    int numCandidates = 0;

    for (int idx = 8; idx <= 35; idx++) {
        for (int mn = 0; tryModules[mn] && numCandidates < 128; mn++) {
            void* scope = TryGetScope(schema, idx, tryModules[mn]);
            if (!scope) continue;

            bool dup = false;
            for (int c = 0; c < numCandidates; c++) {
                if (candidates[c].scope == scope && candidates[c].vfunc == idx) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            candidates[numCandidates].vfunc = idx;
            candidates[numCandidates].module = tryModules[mn];
            candidates[numCandidates].scope = scope;
            numCandidates++;

            printf("    vfunc %2d + %-20s -> 0x%p\n", idx, tryModules[mn], scope);
        }
    }

    printf("\n    Candidates: %d\n\n", numCandidates);

    if (numCandidates == 0) {
        printf("[-] No scope candidates\n");
        return false;
    }

    printf("[*] Phase 2: Finding class lookup...\n\n");

    const char* testClasses[] = {
        "C_BaseEntity", "CBaseEntity",
        "CEntityInstance", "CGameSceneNode",
        "C_DOTA_BaseNPC", "CDOTA_BaseNPC",
        "C_BaseModelEntity", "CBaseModelEntity",
        nullptr
    };

    for (int ci = 0; ci < numCandidates; ci++) {
        void* scope = candidates[ci].scope;

        for (int classIdx = 1; classIdx <= 40; classIdx++) {
            for (int tc = 0; testClasses[tc]; tc++) {
                for (int nargs = 1; nargs <= 2; nargs++) {
                    void* r = TryFindClass(scope, classIdx, testClasses[tc], nargs);
                    if (!r) continue;

                    int nameOff = -1;
                    int indirectOff = -1;
                    if (!CheckNameInResult(r, testClasses[tc], &nameOff, &indirectOff))
                        continue;

                    void* binding = r;
                    if (indirectOff >= 0) {
                        void* inner = nullptr;
                        if (!SEH_ReadPtr(r, indirectOff, &inner)) continue;
                        if (!inner || !SEH_IsReadable(inner, 0x60)) continue;
                        binding = inner;
                    }

                    if (!TryBindingLayout(binding, testClasses[tc], nameOff))
                        continue;

                    g_VFuncIdx.findTypeScopeForModule = candidates[ci].vfunc;
                    g_VFuncIdx.findDeclaredClass = classIdx;
                    g_VFuncIdx.findDeclaredClassNArgs = nargs;
                    g_VFuncIdx.classNameOffset = nameOff;
                    g_VFuncIdx.classInfoPtrOffset = indirectOff;
                    g_VFuncIdx.detected = true;

                    printf("\n    [+] WORKING COMBINATION:\n");
                    printf("        Scope:  vfunc %d, module '%s'\n",
                        candidates[ci].vfunc, candidates[ci].module);
                    printf("        Class:  vfunc %d, %d args\n", classIdx, nargs);
                    printf("        Test:   %s\n", testClasses[tc]);
                    printf("        Name@:  0x%X\n", nameOff);
                    if (indirectOff >= 0)
                        printf("        Indir@: 0x%X\n", indirectOff);

                    int confirmed = 0;
                    for (int v = 0; testClasses[v]; v++) {
                        void* check = TryFindClass(scope, classIdx, testClasses[v], nargs);
                        if (!check) continue;
                        int tn = -1, ti = -1;
                        if (CheckNameInResult(check, testClasses[v], &tn, &ti))
                            confirmed++;
                    }
                    printf("        Confirmed: %d / 8\n\n", confirmed);

                    if (confirmed >= 2) return true;

                    g_VFuncIdx.detected = false;
                    g_BindingLayout.detected = false;
                }
            }
        }
    }

    printf("[-] No working combination found\n\n");
    printf("[*] Diagnostic: showing all non-null class lookups...\n\n");

    for (int ci = 0; ci < numCandidates && ci < 5; ci++) {
        void* scope = candidates[ci].scope;
        printf("  Scope %d (vfunc %d, %s, 0x%p):\n",
            ci, candidates[ci].vfunc, candidates[ci].module, scope);
        DumpHex("    scope", scope, 64);

        int shown = 0;
        for (int idx = 1; idx <= 30 && shown < 5; idx++) {
            void* r = TryFindClass(scope, idx, "C_BaseEntity", 1);
            if (!r) r = TryFindClass(scope, idx, "C_BaseEntity", 2);
            if (!r) continue;
            printf("    vfunc %d -> 0x%p\n", idx, r);
            DumpHex("      result", r, 80);

            for (int off = 0; off <= 0x40; off += 0x08) {
                void* inner = nullptr;
                if (SEH_ReadPtr(r, off, &inner) && inner && inner != r &&
                    SEH_IsReadable(inner, 0x40)) {
                    for (int no = 0; no <= 0x40; no += 0x08) {
                        const char* s = ReadAsciiStr(inner, no);
                        if (s) {
                            printf("      [off=0x%X -> inner 0x%p, str@0x%X = \"%s\"]\n",
                                off, inner, no, s);
                        }
                    }
                }
            }

            for (int no = 0; no <= 0x60; no += 0x08) {
                const char* s = ReadAsciiStr(r, no);
                if (s) printf("      [direct str@0x%X = \"%s\"]\n", no, s);
            }

            shown++;
        }
        printf("\n");
    }

    return false;
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
        printf("\n[-] Schema detection failed.\n");
        printf("[-] This build may have changed the Schema System interface.\n");
        printf("[-] Check diagnostic output above.\n");
        return false;
    }

    printf("[+] Schema system fully detected!\n\n");
    return true;
}
