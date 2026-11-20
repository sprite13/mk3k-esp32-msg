idf_component_get_property(overridden_dir ${COMPONENT_NAME} COMPONENT_OVERRIDEN_DIR)
idf_component_register(
    INCLUDE_DIRS "${overridden_dir}/include"
    REQUIRES esp_event esp_netif esp_phy
)
