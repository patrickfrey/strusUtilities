cmake_minimum_required(VERSION 2.8 FATAL_ERROR )

# must be done BEFORE project in order to work!
include(cmake/install_prefix.cmake)

# Project globals:
project( StrusUtilities )
set( STRUS_MAJOR_VERSION 0 )
set( STRUS_MINOR_VERSION 15 )
set( STRUS_PATCH_VERSION 4 )
set( STRUS_VERSION ${STRUS_MAJOR_VERSION}.${STRUS_MINOR_VERSION}.${STRUS_PATCH_VERSION} )

if(NOT STRUS_ALL)
include( cmake/dist.cmake )
include( cmake/LinuxDistribution.cmake )
include( cmake/build_rules.cmake )
include( cmake/boost.cmake)
include( cmake/intl.cmake )
include( cmake/find_strus_package.cmake )
endif()

find_strus_package( base )
find_strus_package( core )
find_strus_package( analyzer )
find_strus_package( trace )
find_strus_package( module )
find_strus_package( pattern )
find_strus_package( vector )
find_strus_package( rpc )

IF (WITH_STRUS_VECTOR STREQUAL "YES")
set( STRUS_VECTOR_STD_ENABLED 1 )
MESSAGE( STATUS  "Standard module and tests for strusVector enabled." )
ELSE (WITH_STRUS_VECTOR STREQUAL "YES")
set( STRUS_VECTOR_STD_ENABLED 0 )
MESSAGE( STATUS  "Standard module and tests for strusVector not enabled (WITH_STRUS_VECTOR != 'YES')." )
ENDIF (WITH_STRUS_VECTOR STREQUAL "YES")

IF (WITH_STRUS_PATTERN STREQUAL "YES")
set( STRUS_PATTERN_STD_ENABLED 1 )
MESSAGE( STATUS  "Standard module and tests for strusPattern enabled." )
ELSE (WITH_STRUS_PATTERN STREQUAL "YES")
set( STRUS_PATTERN_STD_ENABLED 0 )
MESSAGE( STATUS  "Standard module and tests for strusPattern not enabled (WITH_STRUS_PATTERN != 'YES')." )
ENDIF (WITH_STRUS_PATTERN STREQUAL "YES")

set( CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" )
configure_file( "${PROJECT_SOURCE_DIR}/include/private/version.hpp.in"  "${PROJECT_SOURCE_DIR}/include/private/version.hpp"  @ONLY )

if( APPLE OR WIN32 )
	set( PROJECT_BIN_AFFIX "${CMAKE_BUILD_TYPE}/" )
else( APPLE OR WIN32 )
	set( PROJECT_BIN_AFFIX "" )
endif( APPLE OR WIN32 )

set( STRUS_GETTEXT_PACKAGE "strus-dom" )	#... 1st parameter of bindtextdomain(...)
set( STRUS_GETTEXT_LOCALEDIR "" )			#... 2nd parameter of bindtextdomain(...)
configure_file( "${PROJECT_SOURCE_DIR}/src/utils/internationalization.cpp.in"  "${PROJECT_SOURCE_DIR}/src/utils/internationalization.cpp"  @ONLY )
configure_file( "${PROJECT_SOURCE_DIR}/dist/obs/deploy_to_obs.sh.in"  "${PROJECT_SOURCE_DIR}/dist/obs/deploy_to_obs.sh"  @ONLY )
configure_file( "${PROJECT_SOURCE_DIR}/dist/archlinux/PKGBUILD.in"  "${PROJECT_SOURCE_DIR}/dist/archlinux/PKGBUILD"  @ONLY )
configure_file( "${PROJECT_SOURCE_DIR}/dist/obs/strusutilities.dsc.in"  "${PROJECT_SOURCE_DIR}/dist/obs/strusutilities.dsc"  @ONLY )
configure_file( "${PROJECT_SOURCE_DIR}/dist/redhat/strusutilities.spec.in"  "${PROJECT_SOURCE_DIR}/dist/redhat/strusutilities.spec"  @ONLY )

enable_testing()

# Path declarations:
set( PROGRAM_INCLUDE_DIRS          "${CMAKE_CURRENT_BINARY_DIR}/include"  "${PROJECT_SOURCE_DIR}/include" )
set( PROGRAM_LIBRARY_DIRS          "${CMAKE_CURRENT_BINARY_DIR}/src/program" )
set( UTILS_INCLUDE_DIRS                 "${CMAKE_CURRENT_BINARY_DIR}/include"  "${PROJECT_SOURCE_DIR}/include" )
set( UTILS_LIBRARY_DIRS                 "${CMAKE_CURRENT_BINARY_DIR}/src/utils" )

# Subdirectories:
add_subdirectory( src )
add_subdirectory( tests )
  
if(NOT STRUS_ALL)
include( cmake/report_build_settings.cmake )
endif()

# Install header files:
install(DIRECTORY include/strus
          DESTINATION include
          FILES_MATCHING PATTERN "*.hpp")

if(NOT STRUS_ALL)
include( cmake/standard_targets.cmake )
endif()
