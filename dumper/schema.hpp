#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <cstdint>
#include <cstdio>

#include "seh_helper.h"

struct SchemaField_t {
    const char* m_name;       // 0x00
    void*       m_type;       // 0x08
    int32_t     m_offset;     // 0x10
    int32_t     m_meta_count; // 0x14
    void*       m_meta;       // 0x18
};

struct BindingLayout {
    int name_offset;        // offset of const char* m_name
    int size_offset;        // offset of int32 m_size
    int field_count_offset; // offset of int16 m_field_count
    int fields_offset;      // offset of SchemaField_t* m_fields
    int parent_offset;      // offset of parent binding ptr
    bool detected;
};

struct SchemaVFuncIndices {
    int findTypeScopeForModule;  // CSchemaSystem
    int findDeclaredClass;       // CSchemaSystemTypeScope
    bool detected;
};


extern void*              g_pSchema;
extern BindingLayout      g_BindingLayout;
extern SchemaVFuncIndices g_VFuncIdx;

bool SchemaInit();
void* GetInterface(const char* moduleName, const char* ifaceName);

void* Schema_FindTypeScope(const char* moduleName);
void* Schema_FindClass(void* scope, const char* className);
