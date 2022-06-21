############################ begin_copyright_notice ############################
### Copyright (C) 2015-2021 Intel Corporation
### 
### SPDX-License-Identifier: MIT
############################ end_copyright_notice ##############################

cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)   # required for setting MSVC type

project (GTPin_Examples)

set ( CMAKE_CXX_STANDARD 11 )
set ( CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" )

if (WIN32)
     set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /WX" )
elseif (LINUX)
    set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror" )
endif ()

###### setup architecture ######
#ARCH is not specified setting it to intel64
if (NOT DEFINED ARCH)
    set ( ARCH "intel64")
endif()
message( STATUS "ARCH = ${ARCH}")

if (WIN32)
    add_definitions(-DTARGET_WINDOWS)
elseif ( UNIX ) #linux and mac
    if ( MAC )
        add_definitions(-DTARGET_MAC)
    else ()
        add_definitions(-DTARGET_LINUX)
    endif ()
endif()

###### setup GTPin kit ######
if ( DEFINED ENV{GTPIN_KIT} )
    message( STATUS "GTPIN kit used form env: $ENV{GTPIN_KIT}" )
    set ( GTPIN_KIT ENV{GTPIN_KIT} )
    #get_filename_component ( GTPIN_KIT ENV{GTPIN_KIT} ABSOLUTE )
elseif ( DEFINED GTPIN_KIT )
    message( STATUS "GTPIN kit used: ${GTPIN_KIT}" )
else ()
    message( FATAL_ERROR "Missing GTPin kit" )
endif ()

set ( INSTALL_TRG ${CMAKE_BINARY_DIR}/Bin )
if (WIN32)
    set (RUNTIME "RUNTIME") # used for install only dll's on windows
endif ()

set ( EXAMPLES          funtime latency itrace  memorytrace localmemorytrace perftrace opcodeprof simdprof kernel_weight null)
set ( EXTERNAL_EXAMPLES funtime latency itrace  memorytrace localmemorytrace perftrace opcodeprof simdprof kernel_weight null)

if ( KIT_TYPE STREQUAL "Internal" )
    list (APPEND EXAMPLES nothing insert_ins remove_ins elf fat_binary find_not_running check_static_info count_completion test_type)
endif()

include_directories ( utils )
include_directories ( ${GTPIN_KIT}/Include )
include_directories ( ${GTPIN_KIT}/Include/api )
include_directories ( ${GTPIN_KIT}/Include/ged/${ARCH} )
include_directories ( ${GTPIN_KIT}/common )
link_directories    ( ${GTPIN_KIT}/Lib/${ARCH} )

set (UTILS
            utils/gtpin_crt_utils.cpp
            utils/knob_parser.cpp
            utils/trace_utils.cpp
            utils/gen_send_decoder.cpp
            utils/kernel_exec_config.cpp
            )
add_library( gtpintool_utils STATIC ${UTILS} )
target_link_libraries ( gtpintool_utils gtpin ged )

set_property(TARGET gtpintool_utils PROPERTY POSITION_INDEPENDENT_CODE ON)

add_library( funtime SHARED funtime.cpp )
add_library( latency SHARED latency.cpp )
add_library( itrace SHARED itrace.cpp itrace_utils.cpp )
add_library( memorytrace SHARED memorytrace.cpp kernel_weight.cpp)
add_library( localmemorytrace SHARED localmemorytrace.cpp kernel_weight.cpp)
add_library( perftrace SHARED perftrace.cpp )
add_library( opcodeprof SHARED opcodeprof.cpp opcodeprof_utils.cpp )
add_library( simdprof SHARED simdprof.cpp )
add_library( kernel_weight SHARED kernel_weight.cpp )
add_library( null SHARED null.c )
if ( KIT_TYPE STREQUAL "Internal" )
    add_library( nothing SHARED nothing.cpp )
    add_library( insert_ins SHARED insert_ins.cpp )
    add_library( remove_ins SHARED remove_ins.cpp )
    add_library( elf SHARED elf.cpp )
    add_library( fat_binary SHARED driver_ci/fat_binary.cpp )
    add_library( find_not_running SHARED driver_ci/find_not_running.cpp )
    add_library( check_static_info SHARED driver_ci/check_static_info.cpp )
    add_library( count_completion SHARED driver_ci/count_completion.cpp )
    add_library( test_type SHARED driver_ci/test_type.cpp )
endif()

target_compile_definitions(kernel_weight PUBLIC KERNEL_WEIGHT_STANDALONE)

# set required link libraries
foreach (trg ${EXAMPLES} )
    target_link_libraries ( ${trg} gtpintool_utils )
    target_link_libraries ( ${trg} gtpin ged )
    target_sources( ${trg} PUBLIC ${RESOURCE_FILE} )
endforeach (trg)

# remove auto generated 'lib' prefix for shared object
if (NOT WIN32)
    set_property( TARGET ${EXAMPLES} PROPERTY PREFIX "")
endif ()

if( ${CMAKE_SYSTEM_NAME} STREQUAL "Linux" )
    set_property ( TARGET ${EXAMPLES}
                   PROPERTY INSTALL_RPATH "$ORIGIN/../../Lib/${ARCH}" )
endif()

install ( TARGETS ${EXAMPLES} ${RUNTIME} DESTINATION ${INSTALL_TRG} )