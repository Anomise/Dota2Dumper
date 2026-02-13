#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <cstdint>
#include <cstdio>

#include "seh_helper.h"

struct SchemaField_t {
    const char* m_name;
    void*       m_type;
    int32_t     m_offset;
    int32_t     m_meta_count;
    void*       m_meta;
};

struct BindingLayout {
    int name_offset;
    int size_offset;
    int field_count_offset;
    int fields_offset;
    int parent_offset;
    bool detected;
};

struct SchemaVFuncIndices {
    int findTypeScopeForModule;
    int findDeclaredClass;
    int findDeclaredClassNArgs;
    int classNameOffset;
    int classInfoPtrOffset;
    bool detected;
};

extern void*              g_pSchema;
extern BindingLayout      g_BindingLayout;
extern SchemaVFuncIndices g_VFuncIdx;

bool SchemaInit();
void* GetInterface(const char* moduleName, const char* ifaceName);
void* Schema_FindTypeScope(const char* moduleName);
void* Schema_FindClass(void* scope, const char* className);
