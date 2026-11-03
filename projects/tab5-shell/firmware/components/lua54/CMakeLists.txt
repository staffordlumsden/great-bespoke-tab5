file(GLOB LUA_SOURCES "src/*.c")
idf_component_register(SRCS ${LUA_SOURCES} INCLUDE_DIRS "src")
target_compile_definitions(${COMPONENT_LIB} PRIVATE LUAI_MAXCCALLS=24)
