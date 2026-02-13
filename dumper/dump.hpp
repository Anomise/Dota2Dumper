#pragma once
#include "schema.hpp"
#include <string>
#include <vector>

struct DumpField {
    std::string name;
    std::string type;
    int32_t     offset;
};

struct DumpClass {
    std::string             name;
    std::string             parent;
    int32_t                 size;
    std::vector<DumpField>  fields;
};

class Dumper {
public:
    void DumpModule(const char* moduleName);
    void SaveHpp(const char* path);
    void SaveJson(const char* path);
    size_t ClassCount() const { return m_classes.size(); }

private:
    std::vector<DumpClass> m_classes;
    bool TryDumpClass(void* scope, const char* name);
};
