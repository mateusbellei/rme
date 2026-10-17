cmake_minimum_required(VERSION 3.16)
project(rme_procedural_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(wxWidgets REQUIRED COMPONENTS core base aui html gl net)
include(${wxWidgets_USE_FILE})
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost REQUIRED COMPONENTS thread)

add_executable(procedural_mask_tests
    procedural_mask_tests.cpp
    ../source/json/json_spirit_reader.cpp
    ../source/json/json_spirit_value.cpp
)
target_include_directories(procedural_mask_tests PRIVATE ../source ${Boost_INCLUDE_DIRS})
target_compile_definitions(procedural_mask_tests PRIVATE wxNO_LIB)
target_link_libraries(procedural_mask_tests PRIVATE ${wxWidgets_LIBRARIES} Boost::thread)

# Compile the actual editor integration too; no client assets or GUI are needed.
add_library(procedural_integration_compile OBJECT
    ../source/procedural_common.cpp
    ../source/image_mask_generator.cpp
    ../source/procedural_dialog.cpp
)
target_include_directories(procedural_integration_compile PRIVATE
    ../source ../source/ext/glad/include ../source/ext/glm ${Boost_INCLUDE_DIRS})
target_compile_definitions(procedural_integration_compile PRIVATE wxNO_LIB)
if(MSVC)
    target_compile_options(procedural_mask_tests PRIVATE /bigobj)
    target_compile_options(procedural_integration_compile PRIVATE /bigobj /Zc:strictStrings-)
endif()

enable_testing()
add_test(NAME procedural_masks COMMAND procedural_mask_tests)
set_tests_properties(procedural_masks PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
