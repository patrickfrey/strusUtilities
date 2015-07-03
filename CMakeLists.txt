cmake_minimum_required(VERSION 2.6 FATAL_ERROR )

# must be done BEFORE project in order to work!
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(NOT WIN32)
    set( CMAKE_INSTALL_PREFIX /usr/local )
    if(NOT LIB_INSTALL_DIR)
      set( LIB_INSTALL_DIR lib )
    endif(NOT LIB_INSTALL_DIR)
  endif(NOT WIN32)
else(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(NOT LIB_INSTALL_DIR)
      set( LIB_INSTALL_DIR lib )
    endif(NOT LIB_INSTALL_DIR)
endif(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
set( CMAKE_BUILD_WITH_INSTALL_RPATH FALSE ) 
set( CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${LIB_INSTALL_DIR}/strus" )
set( CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE )

# Project globals:
project( StrusUtilities )
set( STRUS_MAJOR_VERSION 0 )
set( STRUS_MINOR_VERSION 1 )
set( STRUS_PATCH_VERSION 2 )
set( STRUS_VERSION ${STRUS_MAJOR_VERSION}.${STRUS_MINOR_VERSION}.${STRUS_PATCH_VERSION} )

include(cmake/dist.cmake)
include(cmake/LinuxDistribution.cmake)

set_property(GLOBAL PROPERTY rule_launch_compile ccache)
set_property(GLOBAL PROPERTY rule_launch_link ccache)

if(CMAKE_COMPILER_IS_GNUCXX)
set( STRUS_OPTIMIZATION_LEVEL "3" )
set( CMAKE_CXX_FLAGS "-std=c++98  -Wall -pedantic -g -Wfatal-errors -Werror -fvisibility=hidden -fPIC -O${STRUS_OPTIMIZATION_LEVEL}" )
set( CMAKE_C_FLAGS "-std=c99 -Wall -pedantic -Wfatal-errors -Werror -fPIC -O${STRUS_OPTIMIZATION_LEVEL}" )
endif()

if(WIN32)
set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS} /D_WIN32_WINNT=0x0504")
endif()

find_package( Boost 1.53.0 COMPONENTS thread system date_time )
set( Strus_PREFIX_PATH  "${CMAKE_PREFIX_PATH}" )
find_package( strus QUIET )
IF (${strus_FOUND})
   MESSAGE( STATUS  "Found strus package ")
ELSE (${strus_FOUND})
   MESSAGE( STATUS  "Could not locate package strus. Setting strus prefix path to ${CMAKE_INSTALL_PREFIX}")
   set( strus_LIBRARY_DIRS "${CMAKE_INSTALL_PREFIX}/${LIB_INSTALL_DIR}/strus" )
   set( strus_LIBRARIES "-lstrus_database_leveldb -lstrus_storage -lstrus_queryproc -lstrus_queryeval -lstrus_utils" )
   set( strus_INCLUDE_DIRS "${CMAKE_INSTALL_PREFIX}/include/strus" )
   MESSAGE( STATUS  "Set strus include directories to ${strus_INCLUDE_DIRS}" )
   MESSAGE( STATUS  "Set strus library directories to ${strus_LIBRARY_DIRS}" )
   MESSAGE( STATUS  "Set strus libraries to ${strus_LIBRARIES}" )
ENDIF (${strus_FOUND})

find_package( strusanalyzer QUIET )
IF (${strusanalyzer_FOUND})
   MESSAGE( STATUS  "Found strusanalyzer package ")
ELSE (${strusanalyzer_FOUND})
   MESSAGE( STATUS  "Could not locate package strusanalyzer. Setting strus prefix path to ${CMAKE_INSTALL_PREFIX}")
   set( strusanalyzer_LIBRARY_DIRS "${CMAKE_INSTALL_PREFIX}/${LIB_INSTALL_DIR}/strus" )
   set( strusanalyzer_LIBRARIES 
           "-lstrus_analyzer -lstrus_textproc -lstrus_segmenter_textwolf -lstrus_tokenizer_word -lstrus_tokenizer_punctuation -lstrus_normalizer_snowball -lstemmer" )
   set( strusanalyzer_INCLUDE_DIRS "${CMAKE_INSTALL_PREFIX}/include/strus" )
   MESSAGE( STATUS  "Set strusanalyzer include directories to ${strusanalyzer_INCLUDE_DIRS}" )
   MESSAGE( STATUS  "Set strusanalyzer library directories to ${strusanalyzer_LIBRARY_DIRS}" )
   MESSAGE( STATUS  "Set strusanalyzer libraries to ${strusanalyzer_LIBRARIES}" )
ENDIF (${strusanalyzer_FOUND})

find_package( strusmodule QUIET )
IF (${strusmodule_FOUND})
   MESSAGE( STATUS  "Found strusmodule package ")
ELSE (${strusmodule_FOUND})
   MESSAGE( STATUS  "Could not locate package strusmodule. Setting strus prefix path to ${CMAKE_INSTALL_PREFIX}")
   set( strusmodule_LIBRARY_DIRS "${CMAKE_INSTALL_PREFIX}/${LIB_INSTALL_DIR}/strus" )
   set( strusmodule_LIBRARIES  "-lstrus_module" )
   set( strusmodule_INCLUDE_DIRS "${CMAKE_INSTALL_PREFIX}/include/strus" )
   MESSAGE( STATUS  "Set strusmodule include directories to ${strusmodule_INCLUDE_DIRS}" )
   MESSAGE( STATUS  "Set strusmodule library directories to ${strusmodule_LIBRARY_DIRS}" )
   MESSAGE( STATUS  "Set strusmodule libraries to ${strusmodule_LIBRARIES}" )
ENDIF (${strusmodule_FOUND})

find_package( strusrpc QUIET )
IF (${strusrpc_FOUND})
   MESSAGE( STATUS  "Found strusrpc package ")
ELSE (${strusrpc_FOUND})
   MESSAGE( STATUS  "Could not locate package strusrpc. Setting strus prefix path to ${CMAKE_INSTALL_PREFIX}")
   set( strusrpc_LIBRARY_DIRS "${CMAKE_INSTALL_PREFIX}/${LIB_INSTALL_DIR}/strus" )
   set( strusrpc_LIBRARIES  "-lstrus_rpc_client -lstrus_rpc_client_socket" )
   set( strusrpc_INCLUDE_DIRS "${CMAKE_INSTALL_PREFIX}/include/strus" )
   MESSAGE( STATUS  "Set strusrpc include directories to ${strusrpc_INCLUDE_DIRS}" )
   MESSAGE( STATUS  "Set strusrpc library directories to ${strusrpc_LIBRARY_DIRS}" )
   MESSAGE( STATUS  "Set strusrpc libraries to ${strusrpc_LIBRARIES}" )
ENDIF (${strusrpc_FOUND})

set( CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" )
set( CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON )
configure_file( "${PROJECT_SOURCE_DIR}/include/private/version.hpp.in"  "${PROJECT_SOURCE_DIR}/include/private/version.hpp"  @ONLY )

enable_testing()

# Subdirectories:
add_subdirectory( src )
  
# Message:
MESSAGE("Platform:")
MESSAGE("  Host: ${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_VERSION} ${CMAKE_HOST_SYSTEM_PROCESSOR}")
MESSAGE("  Canonical: ${INSTALLER_PLATFORM}")
MESSAGE("  CMake: ${CMAKE_VERSION}")
MESSAGE("  CMake generator: ${CMAKE_GENERATOR}")
MESSAGE("  CMake build tool: ${CMAKE_BUILD_TOOL}")

MESSAGE("Compiler:")
MESSAGE("  C++ compilation flags: ${CMAKE_CXX_FLAGS}")
MESSAGE("  C compilation flags: ${CMAKE_C_FLAGS}")

# Install header files:
install(DIRECTORY include/strus
          DESTINATION include
          FILES_MATCHING PATTERN "*.hpp")

# Uninstall target:
configure_file(
    "${CMAKE_MODULE_PATH}/uninstall.cmake.in"
    "${CMAKE_MODULE_PATH}/uninstall.cmake"
    IMMEDIATE @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_MODULE_PATH}/uninstall.cmake")


# Make distclean:
IF (UNIX)
  ADD_CUSTOM_TARGET (distclean @echo cleaning for source distribution)
  SET(DISTCLEANED
   cmake.depends
   cmake.check_depends
   CMakeCache.txt
   cmake.check_cache
   *.cmake
   Makefile
   core core.*
   *~
  )
  
  ADD_CUSTOM_COMMAND(
    DEPENDS clean
    COMMENT "distribution clean"
    COMMAND rm
    ARGS    -Rf pkg ${DISTCLEANED}
    TARGET  distclean
  )
ENDIF(UNIX)

