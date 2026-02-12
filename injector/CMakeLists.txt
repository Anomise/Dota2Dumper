add_executable(injector main.cpp)

target_compile_definitions(injector PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(injector PRIVATE psapi)
