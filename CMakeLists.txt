cmake_minimum_required(VERSION 2.6 FATAL_ERROR )

# must be done BEFORE project in order to work!
include(cmake/install_prefix.cmake)

# Project globals:
project( StrusUtilities )
set( STRUS_MAJOR_VERSION 0 )
set( STRUS_MINOR_VERSION 1 )
set( STRUS_PATCH_VERSION 6 )
set( STRUS_VERSION ${STRUS_MAJOR_VERSION}.${STRUS_MINOR_VERSION}.${STRUS_PATCH_VERSION} )

include( cmake/dist.cmake )
include( cmake/LinuxDistribution.cmake )
include( cmake/build_rules.cmake )
include( cmake/find_strus_package.cmake )

find_package( Boost 1.53.0 COMPONENTS thread system date_time )
find_strus_package( core )
find_strus_package( analyzer )
find_strus_package( module )
find_strus_package( rpc )

set( CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" )
set( CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON )
configure_file( "${PROJECT_SOURCE_DIR}/include/private/version.hpp.in"  "${PROJECT_SOURCE_DIR}/include/private/version.hpp"  @ONLY )

enable_testing()

# Subdirectories:
add_subdirectory( src )
  
include( cmake/report_build_settings.cmake )

# Install header files:
install(DIRECTORY include/strus
          DESTINATION include
          FILES_MATCHING PATTERN "*.hpp")

include( cmake/standard_targets.cmake )

