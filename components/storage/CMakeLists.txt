idf_component_register(
    SRCS "src/save_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES common nvs_flash log spiffs
)
