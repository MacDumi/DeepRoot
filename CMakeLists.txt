cmake_minimum_required(VERSION 3.16)

# Standard ESP-IDF project layout. This same tree builds either with
# `idf.py` directly or via PlatformIO's `framework = espidf`.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(matter_soil_sensor)

# ---------------------------------------------------------------------------
# Build the Matter stack as C++17.
#
# connectedhomeip targets C++17; ESP-IDF 5.4+ defaults to -std=gnu++2b.
# Under C++20 and later, std::optional's operator== constraint is not
# satisfied for types whose operator== takes a BASE type (as the closure
# cluster's GenericOverallCurrentState does), so esp_matter 1.5.x fails
# to compile with:
#
#   Nullable.h:116: no match for 'operator==' (operand types are
#   'const std::optional<...GenericOverallCurrentState>' and same)
#
# Verified: the failure depends on the LANGUAGE STANDARD, not the compiler
# version — GCC 15 and 16 both compile it at c++17 and both reject it at
# c++20/c++2b. Using an older GCC does not help.
#
# Scoping this to the Matter component only; the rest of the project keeps
# the IDF default.
# ---------------------------------------------------------------------------
idf_component_get_property(matter_lib espressif__esp_matter COMPONENT_LIB)
target_compile_options(${matter_lib} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
