add_library(dumper SHARED
    dllmain.cpp
    schema.hpp
    schema.cpp
    dump.hpp
    dump.cpp
)

target_compile_definitions(dumper PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(dumper PRIVATE /EHa)

target_link_libraries(dumper PRIVATE psapi)
