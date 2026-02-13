add_library(dumper SHARED
    dllmain.cpp
    schema.hpp
    schema.cpp
    seh_helper.h
    seh_helper.cpp
    dump.hpp
    dump.cpp
)

target_compile_definitions(dumper PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(dumper PRIVATE psapi)
