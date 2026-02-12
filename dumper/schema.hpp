#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>


template <typename Ret, typename... Args>
Ret CallVFunc(void* inst, int idx, Args... args) {
    using Fn = Ret(__thiscall*)(void*, Args...);
    auto vt = *reinterpret_cast<void***>(inst);
    return reinterpret_cast<Fn>(vt[idx])(inst, args...);
}


struct SchemaField_t {
    const char* m_name;       // 0x00
    void*       m_type;       // 0x08
    int32_t     m_offset;     // 0x10
    int32_t     m_meta_count; // 0x14
    void*       m_meta;       // 0x18
};

class CSchemaClassBinding {
public:
    void*           m_parent;       // 0x00
    const char*     m_binary_name;  // 0x08
    const char*     m_module_name;  // 0x10
    int32_t         m_size;         // 0x18
    int16_t         m_field_count;  // 0x1C
    int16_t         m_pad1;         // 0x1E
    int32_t         m_pad2;         // 0x20
    uint64_t        m_pad3;         // 0x24 — alignment может быть разный
    SchemaField_t*  m_fields;       // 0x28

    const char*     GetName()       const { return m_binary_name; }
    int16_t         GetFieldCount() const { return m_field_count; }
    SchemaField_t*  GetFields()     const { return m_fields; }

    const char* GetParentName() const {
        if (!m_parent) return nullptr;
        return reinterpret_cast<const CSchemaClassBinding*>(m_parent)->m_binary_name;
    }
};


class CSchemaSystemTypeScope {
public:
    CSchemaClassBinding* FindDeclaredClass(const char* name) {
        return CallVFunc<CSchemaClassBinding*>(this, 2, name);
    }
};


class CSchemaSystem {
public:
    CSchemaSystemTypeScope* FindTypeScopeForModule(const char* mod) {
        return CallVFunc<CSchemaSystemTypeScope*>(this, 13, mod, nullptr);
    }
};

extern CSchemaSystem* g_pSchema;
bool SchemaInit();

void* GetInterface(const char* moduleName, const char* ifaceName);
