idf_component_register(
    SRCS "app_main.cpp" "soil_endpoint.cpp" "soil_probe.cpp" "factory_info.cpp"
         "battery.cpp" "led.cpp" "calibration.cpp" "ble_gate.cpp"
         "adc_bus.cpp" "antenna.cpp" "button.cpp" "commissioning.cpp"
         "icd_support.cpp" "matter_node.cpp" "measurement.cpp"
         "moisture_display.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_ledc nvs_flash esp_pm bt
                 ieee802154
)

# Keep the BLE controller off on a commissioned boot. See ble_gate.h.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=nimble_port_init")
