file(GLOB MPY_PY_SRCS "py/*.c")
file(GLOB MPY_PORT_SRCS "port/*.c")
file(GLOB MPY_SHARED_SRCS "shared/runtime/*.c")

idf_component_register(
    SRCS ${MPY_PY_SRCS} ${MPY_PORT_SRCS} ${MPY_SHARED_SRCS}
    INCLUDE_DIRS "." "py" "genhdr" "port" "shared/runtime")

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-sign-compare
    -Wno-old-style-declaration
    -Wno-maybe-uninitialized
    -Wno-implicit-fallthrough)
