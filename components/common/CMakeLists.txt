idf_component_register(
    SRCS
        "src/project_catalog.c"
        "src/project_types.c"
        "src/time_utils.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer log
)
