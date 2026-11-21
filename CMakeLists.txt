cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(matter_soil_sensor)

# ---------------------------------------------------------------------------
# Build the Matter stack as C++17.
#
# connectedhomeip targets C++17; ESP-IDF 5.4+ defaults to -std=gnu++2b.
# Under C++20 and later, std::optional's operator== constraint is not
# satisfied for types whose operator== takes a BASE type (as the closure
# cluster's GenericOverallCurrentState does), so esp_matter 1.5.x fails
# to compile .
# ---------------------------------------------------------------------------
idf_component_get_property(matter_lib espressif__esp_matter COMPONENT_LIB)
target_compile_options(${matter_lib} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)

# ---------------------------------------------------------------------------
# Take the spaces out of OpenThread's build timestamp.
#
# Rewriting the timestamp in ISO-8601 basic form removes the spaces, which
# sends the value down the elif branch that escapes correctly. Same class of
# PlatformIO/CMake translation gap as tools/pio_embed_binary_data.py, but fixed
# here rather than there because CMake owns the property: `idf.py` builds take
# the identical path and are unaffected either way.
# ---------------------------------------------------------------------------
if(CONFIG_OPENTHREAD_ENABLED)
    idf_component_get_property(openthread_lib openthread COMPONENT_LIB)
    get_target_property(ot_defs ${openthread_lib} COMPILE_DEFINITIONS)

    if(ot_defs)
        string(TIMESTAMP ot_stamp "%Y-%m-%dT%H:%M:%SZ" UTC)
        set(ot_defs_fixed "")
        foreach(ot_def IN LISTS ot_defs)
            if(ot_def MATCHES "^OPENTHREAD_BUILD_DATETIME=")
                list(APPEND ot_defs_fixed "OPENTHREAD_BUILD_DATETIME=\"${ot_stamp}\"")
            else()
                list(APPEND ot_defs_fixed "${ot_def}")
            endif()
        endforeach()
        set_target_properties(${openthread_lib} PROPERTIES
                              COMPILE_DEFINITIONS "${ot_defs_fixed}")
    endif()
endif()
