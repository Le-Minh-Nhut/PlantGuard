idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        esp-dl
        yolo26
        esp_http_client
        json
)

set(MODEL_FILE
    "${CMAKE_CURRENT_LIST_DIR}/models/s3/yolo26n_plantguard_320_s8_s3.espdl"
)

idf_build_get_property(component_targets __COMPONENT_TARGETS)

if ("___idf_espressif__esp-dl" IN_LIST component_targets)
    idf_component_get_property(espdl_dir espressif__esp-dl COMPONENT_DIR)
else()
    idf_component_get_property(espdl_dir esp-dl COMPONENT_DIR)
endif()

set(cmake_dir ${espdl_dir}/fbs_loader/cmake)
include(${cmake_dir}/utilities.cmake)

set(MODEL_FILENAME "yolo26n_plantguard_320_s8_s3.espdl")

string(MAKE_C_IDENTIFIER ${MODEL_FILENAME} SAFE_NAME)
set(MODEL_SYMBOL "_binary_${SAFE_NAME}_start")

add_definitions(
    -DMODEL_SYMBOL_STR=\"${MODEL_SYMBOL}\"
)

target_add_aligned_binary_data(
    ${COMPONENT_LIB}
    "${MODEL_FILE}"
    BINARY
)