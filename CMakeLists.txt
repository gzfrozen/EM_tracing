cmake_minimum_required(VERSION 3.18)
project(EM_tracing LANGUAGES CXX CUDA)

if (MSVC)
    # set source code format to UTF-8 
    # suppress warning caused by system language
    add_compile_options(-Xcompiler "/utf-8")
endif()

# ------------------------------------------------------------------
# first, include gdt project to do some general configuration stuff
# (build modes, glut, optix, etc)
# ------------------------------------------------------------------
set(gdt_dir ${PROJECT_SOURCE_DIR}/support/gdt/)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${gdt_dir}/cmake/")
include(${gdt_dir}/cmake/configure_build_type.cmake)
include(${gdt_dir}/cmake/configure_optix.cmake)

# ------------------------------------------------------------------
# import gdt and other submodule
# ------------------------------------------------------------------
find_package(OpenGL REQUIRED)
include_directories(${gdt_dir})
add_subdirectory(${gdt_dir} EXCLUDE_FROM_ALL)
include_directories(${PROJECT_SOURCE_DIR}/support/3rdParty/)

# ------------------------------------------------------------------
# build glfw
# ------------------------------------------------------------------
# Setting directories
set(EM_SRC_DIR ${PROJECT_SOURCE_DIR}/src CACHE PATH "Project source directory")
set(EM_INCLUDE_DIR ${EM_SRC_DIR}/include CACHE PATH "Header file directory")
set(EM_CUDA_DIR ${EM_SRC_DIR}/cuda CACHE PATH "CUDA file directory")

set(OpenGL_GL_PREFERENCE LEGACY)
if (WIN32)
  set(glfw_dir ${PROJECT_SOURCE_DIR}/support/3rdParty/glfw/)
  include_directories(${glfw_dir}/include)
  add_subdirectory(${glfw_dir} EXCLUDE_FROM_ALL)
else()
  find_package(glfw3 REQUIRED)
endif()
include_directories(${PROJECT_SOURCE_DIR}/support/glfWindow)
add_subdirectory(${PROJECT_SOURCE_DIR}/support/glfWindow EXCLUDE_FROM_ALL)

# PTX Generation
add_library(CudaPTX OBJECT 
            ${EM_CUDA_DIR}/rayLaunch.cu
            ${EM_CUDA_DIR}/closestHit.cu
            ${EM_CUDA_DIR}/anyHit.cu
            ${EM_CUDA_DIR}/missHit.cu)
target_include_directories(CudaPTX PRIVATE ${EM_INCLUDE_DIR}
                           PRIVATE ${EM_CUDA_DIR})
target_link_libraries(CudaPTX
  gdt
  ${optix_LIBRARY}
  ${CUDA_LIBRARIES}
  ${CUDA_CUDA_LIBRARY}
  )

# Request that target be built with -std=c++14
# As this is a public compile feature anything that links to it
# will also build with -std=c++14
target_compile_features(CudaPTX PRIVATE cxx_std_14)

# Cuda compile options
target_compile_options(CudaPTX PRIVATE --use_fast_math)

# We need to explicitly state that we need all CUDA files to be compiled into 
# PTX files, and built with -dc as the member functions could be called by
# other libraries and executables
set_target_properties(CudaPTX PROPERTIES 
  CUDA_PTX_COMPILATION ON 
  CUDA_ARCHITECTURES 75-real # Set your GPU architecture (important)
  )

# Precompile headers setting
target_precompile_headers(CudaPTX PRIVATE 
    <optix_device.h>
    <cuda_runtime.h>
    [["gdt/random/random.h"]]
    )

#We are going to need a wrapper around bin2c for multiple reasons
# 1. bin2c only converts a single file at a time
# 2. bin2c has only standard out support, so we have to manually
# redirect to a cmake buffer
# 3. We want to pack everything into a single output file, so we
# need to also pass the --name option
set(output_file ${EM_SRC_DIR}/embedded_objs.cpp)

get_filename_component(cuda_compiler_bin "${CMAKE_CUDA_COMPILER}" DIRECTORY)
find_program(bin_to_c
  NAMES bin2c
  PATHS ${cuda_compiler_bin}
  )
if(NOT bin_to_c)
  message(FATAL_ERROR
    "bin2c not found:\n"
    "  CMAKE_CUDA_COMPILER='${CMAKE_CUDA_COMPILER}'\n"
    "  cuda_compiler_bin='${cuda_compiler_bin}'\n"
    )
endif()

add_custom_command(
  OUTPUT "${output_file}"
  COMMAND ${CMAKE_COMMAND}
    "-DBIN_TO_C_COMMAND=${bin_to_c}"
    "-DOBJECTS=$<TARGET_OBJECTS:CudaPTX>"
    "-DOUTPUT=${output_file}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/bin2c_wrapper.cmake
  VERBATIM
  DEPENDS $<TARGET_OBJECTS:CudaPTX>
  COMMENT "Converting Object files to a C file"
  )

# Main program
add_executable(EM_tracing 
    ${output_file}   
    ${EM_SRC_DIR}/main.cpp 
    ${EM_SRC_DIR}/SampleRenderer.cpp 
    ${EM_SRC_DIR}/Model.cpp 
    )
add_dependencies(EM_tracing CudaPTX)
target_include_directories(EM_tracing PRIVATE ${EM_INCLUDE_DIR})
target_link_libraries(EM_tracing
  gdt
  ${optix_LIBRARY}
  ${CUDA_LIBRARIES}
  ${CUDA_CUDA_LIBRARY}
  glfw
  glfWindow
  ${OPENGL_gl_LIBRARY}
  )

target_compile_features(EM_tracing PRIVATE cxx_std_17)

# Set your OBJ file path
target_compile_definitions(EM_tracing PUBLIC _OBJ_FILE="${PROJECT_SOURCE_DIR}/models/sponza.obj")

set_target_properties(EM_tracing PROPERTIES 
  POSITION_INDEPENDENT_CODE ON )

target_precompile_headers(EM_tracing PRIVATE 
  <assert.h>
  <vector>
  <unordered_map>
 [["optix7.hpp"]]
  <GL/gl.h>
 [["gdt/math/vec.h"]]
 [["gdt/math/AffineSpace.h"]]
 [["tiny_obj_loader.h"]]
 [["stb_image.h"]]
 )

if(APPLE)
# We need to add the default path to the driver (libcuda.dylib) as an rpath,
# so that the static cuda runtime can find it at runtime.
  set_target_property(EM_tracing PROPERTY BUILD_RPATH ${CMAKE_CUDA_IMPLICIT_LINK_DIRECTORIES})
endif()