# Gera git_version.h com o commit atual (roda a cada build).
# "-dirty" indica que havia alterações não commitadas no momento da compilação.
execute_process(
    COMMAND git describe --always --dirty --abbrev=7
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_VERSION)
    set(GIT_VERSION "desconhecido")
endif()

set(CONTENT "#define GIT_VERSION \"${GIT_VERSION}\"\n")
if(EXISTS ${OUTPUT_FILE})
    file(READ ${OUTPUT_FILE} OLD_CONTENT)
endif()
# Só reescreve se mudou, para não recompilar à toa
if(NOT "${CONTENT}" STREQUAL "${OLD_CONTENT}")
    file(WRITE ${OUTPUT_FILE} "${CONTENT}")
endif()
