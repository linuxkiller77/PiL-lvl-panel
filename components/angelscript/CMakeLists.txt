file(GLOB ANGELSCRIPT_SOURCES
    "source/*.cpp"
)

set(ANGELSCRIPT_ADDON_SOURCES
    "add_on/scriptstdstring/scriptstdstring.cpp"
    "add_on/scriptstdstring/scriptstdstring_utils.cpp"
    "add_on/scriptarray/scriptarray.cpp"
    "add_on/scriptmath/scriptmath.cpp"
)

idf_component_register(
    SRCS ${ANGELSCRIPT_SOURCES} ${ANGELSCRIPT_ADDON_SOURCES}
    INCLUDE_DIRS
        "include"
        "source"
        "add_on/scriptstdstring"
        "add_on/scriptarray"
        "add_on/scriptmath"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-error=strict-aliasing
    -Wno-strict-aliasing
    -fno-strict-aliasing

    -Wno-error=empty-body
    -Wno-empty-body

    -Wno-error=cast-function-type
    -Wno-cast-function-type

    -Wno-error=attributes
    -Wno-attributes
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    AS_NO_EXCEPTIONS
    AS_POSIX_THREADS
    AS_MAX_PORTABILITY
)
