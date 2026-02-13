#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <Windows.h>
#include <stdint.h>


void* SEH_VCall1(void* instance, int index, const char* arg1);


void* SEH_VCall2(void* instance, int index, const char* arg1, void* arg2);

int SEH_ReadPtr(const void* base, int offset, void** out);


int SEH_ReadI32(const void* base, int offset, int32_t* out);


int SEH_ReadI16(const void* base, int offset, int16_t* out);

const char* SEH_ReadStr(const void* base, int offset);

int SEH_IsReadable(const void* ptr, size_t size);


int SEH_ValidateString(const char* str, size_t maxLen);

#ifdef __cplusplus
}
#endif
