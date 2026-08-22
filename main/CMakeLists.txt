set(main_srcs
    "main.c"
)

idf_component_register(
    SRCS ${main_srcs}
    INCLUDE_DIRS "."
    REQUIRES web core
)