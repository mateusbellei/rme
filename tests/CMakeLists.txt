cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_DEFAULT_CMP0167 OLD)
project(rme_procedural_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(procedural_world STATIC ../source/world_plan.cpp ../source/world_catalog.cpp ../source/world_decoration.cpp ../source/world_export.cpp)
target_include_directories(procedural_world PUBLIC ../source)
add_executable(procedural_world_tests world_plan_tests.cpp)
target_link_libraries(procedural_world_tests PRIVATE procedural_world)
add_executable(rme_world_plan ../tools/world_plan_cli.cpp)
target_link_libraries(rme_world_plan PRIVATE procedural_world)
enable_testing()
add_test(NAME procedural_world COMMAND procedural_world_tests)
add_executable(procedural_catalog_tests world_catalog_tests.cpp)
target_link_libraries(procedural_catalog_tests PRIVATE procedural_world)
add_test(NAME procedural_catalog COMMAND procedural_catalog_tests)

add_executable(procedural_decoration_tests world_decoration_tests.cpp)
target_link_libraries(procedural_decoration_tests PRIVATE procedural_world)
add_test(NAME procedural_decoration COMMAND procedural_decoration_tests)

option(RME_TEST_EDITOR_INTEGRATION "Build wxWidgets mask tests and editor integration" ON)
if(NOT RME_TEST_EDITOR_INTEGRATION)
    return()
endif()
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
    ../source/world_editor.cpp
    ../source/world_batch.cpp
    ../source/procedural_generator.cpp
    ../source/procedural_recipe.cpp
    ../source/mt_rand.cpp
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

add_executable(procedural_recipe_tests
    recipe_tests.cpp
    ../source/world_inputs.cpp
    ../source/world_catalog_json.cpp
    ../source/procedural_recipe.cpp
    ../source/common.cpp
    ../source/mt_rand.cpp
    ../source/json/json_spirit_reader.cpp
    ../source/json/json_spirit_value.cpp
    ../source/json/json_spirit_writer.cpp
)
target_include_directories(procedural_recipe_tests PRIVATE ../source ${Boost_INCLUDE_DIRS})
target_compile_definitions(procedural_recipe_tests PRIVATE wxNO_LIB)
target_link_libraries(procedural_recipe_tests PRIVATE procedural_world ${wxWidgets_LIBRARIES} Boost::thread)
if(MSVC)
    target_compile_options(procedural_recipe_tests PRIVATE /bigobj /Zc:strictStrings-)
endif()
add_test(NAME procedural_recipes COMMAND procedural_recipe_tests)
set_tests_properties(procedural_recipes PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
