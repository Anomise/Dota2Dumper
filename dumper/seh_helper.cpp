#include "seh_helper.h"
#include <string.h>

void* SEH_VCall1(void* instance, int index, const char* arg1) {
    __try {
        void** vtable;
        void* fn;
        if (!instance) return NULL;
        vtable = *(void***)instance;
        if (!vtable) return NULL;
        fn = vtable[index];
        if (!fn) return NULL;
        {
            typedef void* (__thiscall *VFn)(void*, const char*);
            return ((VFn)fn)(instance, arg1);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

void* SEH_VCall2(void* instance, int index, const char* arg1, void* arg2) {
    __try {
        void** vtable;
        void* fn;
        if (!instance) return NULL;
        vtable = *(void***)instance;
        if (!vtable) return NULL;
        fn = vtable[index];
        if (!fn) return NULL;
        {
            typedef void* (__thiscall *VFn)(void*, const char*, void*);
            return ((VFn)fn)(instance, arg1, arg2);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

int SEH_ReadPtr(const void* base, int offset, void** out) {
    __try {
        *out = *(void**)((uintptr_t)base + offset);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = NULL;
        return 0;
    }
}

int SEH_ReadI32(const void* base, int offset, int32_t* out) {
    __try {
        *out = *(int32_t*)((uintptr_t)base + offset);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
        return 0;
    }
}

int SEH_ReadI16(const void* base, int offset, int16_t* out) {
    __try {
        *out = *(int16_t*)((uintptr_t)base + offset);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
        return 0;
    }
}

const char* SEH_ReadStr(const void* base, int offset) {
    __try {
        const char* str = *(const char**)((uintptr_t)base + offset);
        if (!str) return NULL;
        {
            volatile char c = str[0];
            (void)c;
            c = str[1];
            (void)c;
        }
        if (strlen(str) == 0 || strlen(str) > 256)
            return NULL;
        return str;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

int SEH_IsReadable(const void* ptr, size_t size) {
    __try {
        volatile char sum = 0;
        const char* p = (const char*)ptr;
        sum += p[0];
        if (size > 1) sum += p[size / 2];
        if (size > 2) sum += p[size - 1];
        (void)sum;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int SEH_ValidateString(const char* str, size_t maxLen) {
    __try {
        size_t len;
        if (!str) return 0;
        len = strnlen(str, maxLen + 1);
        if (len == 0 || len > maxLen) return 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int SEH_IsAsciiString(const char* str, size_t maxLen) {
    __try {
        size_t i;
        size_t len;
        if (!str) return 0;
        len = strnlen(str, maxLen + 1);
        if (len == 0 || len > maxLen) return 0;
        for (i = 0; i < len; i++) {
            unsigned char c = (unsigned char)str[i];
            if (c < 0x20 || c > 0x7E) {
                if (c != '_') return 0;
            }
        }
        {
            unsigned char first = (unsigned char)str[0];
            if (first != '_' && first != 'm' &&
                !(first >= 'A' && first <= 'Z') &&
                !(first >= 'a' && first <= 'z'))
                return 0;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
