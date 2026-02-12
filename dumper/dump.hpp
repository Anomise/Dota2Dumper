#pragma once
#include "schema.hpp"
#include <string>
#include <vector>
#include <fstream>

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
    void SaveHpp(const std::string& path);
    void SaveJson(const std::string& path);
    size_t ClassCount() const { return m_classes.size(); }

private:
    std::vector<DumpClass> m_classes;

    bool TryDumpClass(CSchemaSystemTypeScope* scope, const char* name);
    std::string ReadTypeName(void* schemaType);
};
