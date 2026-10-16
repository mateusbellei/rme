cmake_minimum_required(VERSION 3.16)

set(CMAKE_POLICY_DEFAULT_CMP0167 OLD)

project(rme)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(NOT MSVC)
set(CMAKE_CXX_FLAGS                "-Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-deprecated-declarations -Wno-overloaded-virtual -Wno-strict-aliasing -Wno-sign-compare -Wno-unused-function -Wunused-result")
set(CMAKE_CXX_FLAGS_DEBUG          "-O0 -g -D__DEBUG__")
set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE        "-O4 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
endif()

find_package(OpenGL REQUIRED)

if(APPLE)
    set(CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
endif()
find_package(LibArchive REQUIRED)

if(WIN32)
    set(Boost_THREADAPI win32)
endif()
find_package(Boost 1.34.0 COMPONENTS thread system REQUIRED)

find_package(wxWidgets COMPONENTS html aui gl adv core net base REQUIRED)

find_package(GLUT REQUIRED)
find_package(ZLIB REQUIRED)
find_path(GLM_INCLUDE_DIR glm/glm.hpp HINTS "${CMAKE_CURRENT_SOURCE_DIR}/source/ext/glm")
if(NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR "GLM headers are required. Install GLM or set GLM_INCLUDE_DIR to its include root.")
endif()

include(${wxWidgets_USE_FILE})
include(source/CMakeLists.txt)
if(WIN32)
    add_executable(rme WIN32 ${rme_H} ${rme_SRC})
else()
    add_executable(rme ${rme_H} ${rme_SRC})
endif()
option(RME_ENABLE_WORLD_SELF_TEST "Enable asset-free real editor/OTBM regression command" OFF)
if(RME_ENABLE_WORLD_SELF_TEST)
    target_sources(rme PRIVATE tests/world_editor_smoke.cpp)
    target_compile_definitions(rme PRIVATE RME_ENABLE_WORLD_SELF_TEST)
    enable_testing()
    add_test(NAME procedural_world_editor COMMAND rme --world-self-test "${CMAKE_CURRENT_BINARY_DIR}/world-smoke")
    add_test(NAME procedural_world_batch_help COMMAND rme --generate-world --help)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME procedural_world_batch_cli COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/world_batch_cli_test.py"
        "$<TARGET_FILE:rme>" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(MSVC)
    target_compile_options(rme PRIVATE /W3 /bigobj /Zc:strictStrings-)
    target_compile_definitions(rme PRIVATE $<$<CONFIG:Debug>:__DEBUG__>)
endif()

set_target_properties(rme PROPERTIES CXX_STANDARD 20)
set_target_properties(rme PROPERTIES CXX_STANDARD_REQUIRED ON)

target_include_directories(rme PRIVATE
	${CMAKE_SOURCE_DIR}/source
	${CMAKE_SOURCE_DIR}/source/ext/glad/include
	${CMAKE_SOURCE_DIR}/source/ext/glm
	${GLM_INCLUDE_DIR}
)
include_directories(${Boost_INCLUDE_DIRS} ${LibArchive_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIR} ${GLUT_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIR})
target_link_libraries(rme ${wxWidgets_LIBRARIES} ${Boost_LIBRARIES} ${LibArchive_LIBRARIES} ${OPENGL_LIBRARIES} ${GLUT_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_DL_LIBS})
