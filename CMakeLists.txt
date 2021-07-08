cmake_minimum_required (VERSION 3.12)
project (latke)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

add_definitions(-DCL_TARGET_OPENCL_VERSION=220)
add_definitions(-DCL_USE_DEPRECATED_OPENCL_1_2_APIS=1)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

option(XILINX "Support Xilinx XRT" OFF)
if (XILINX)
set(VIVADO_INCLUDE_DIR "/tools/Xilinx/Vivado/2021.1/include/" CACHE STRING "Vivado Include Directory")
set(XRT_INCLUDE_DIR "/opt/xilinx/xrt/include/" CACHE STRING "XRT Include Directory")
include_directories(${VIVADO_INCLUDE_DIR} ${XRT_INCLUDE_DIR})
endif (XILINX)

find_package(OpenCL REQUIRED)
include_directories(${OPENCL_INCLUDE_DIRS} src tests/include)

# Install directories
include(GNUInstallDirs)

# Defines the source code for the library
add_library(latke STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/src/UtilOCL.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/KernelOCL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/IArch.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/ArchAMD.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/ArchFactory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/latke.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/DeviceManagerOCL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DualBufferOCL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DualImageOCL.h
	${CMAKE_CURRENT_SOURCE_DIR}/src/IDualMemOCL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/QueueOCL.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EnqueueInfoOCL.h	

	${CMAKE_CURRENT_SOURCE_DIR}/src/DeviceOCL.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/DeviceOCL.h	
	${CMAKE_CURRENT_SOURCE_DIR}/src/DeviceManagerOCL.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DualBufferOCL.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/DualImageOCL.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/QueueOCL.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/KernelOCL.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UtilOCL.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/EnqueueInfoOCL.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ArchFactory.cpp
    
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform.cl
)

 configure_file(
 ${CMAKE_CURRENT_SOURCE_DIR}/latke_config.h.cmake.in
 ${CMAKE_CURRENT_SOURCE_DIR}/src/latke_config.h
 @ONLY
 )

# tests

# Gather list of all cl files
file(GLOB CLFiles ${CMAKE_CURRENT_SOURCE_DIR}/tests/debayer/*.cl 
                  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cl 
                  ${CMAKE_CURRENT_SOURCE_DIR}/src/latke_config.h )

foreach(CLFile ${CLFiles})
  add_custom_command(TARGET latke PRE_BUILD
                     COMMAND ${CMAKE_COMMAND} -E
                         copy_if_different  ${CLFile}  $<TARGET_FILE_DIR:latke> )
endforeach()

add_executable(debayer_buffer tests/debayer/debayerBuffer.cpp)
target_link_libraries(debayer_buffer latke ${OPENCL_LIBRARIES} Threads::Threads)

add_executable(debayer_image tests/debayer/debayerImage.cpp)
target_link_libraries(debayer_image latke ${OPENCL_LIBRARIES} Threads::Threads)

if (XILINX)
add_executable(wide_vadd tests/wide_vadd/wide_vadd_main.cpp)
target_link_libraries(wide_vadd latke ${OPENCL_LIBRARIES} Threads::Threads)
endif()


