cmake_minimum_required(VERSION 2.6 FATAL_ERROR )

# must be done BEFORE project in order to work!
include(cmake/install_prefix.cmake)

# Project globals:
project( StrusUtilities )
set( STRUS_MAJOR_VERSION 0 )
set( STRUS_MINOR_VERSION 4 )
set( STRUS_PATCH_VERSION 1 )
set( STRUS_VERSION ${STRUS_MAJOR_VERSION}.${STRUS_MINOR_VERSION}.${STRUS_PATCH_VERSION} )

include( cmake/dist.cmake )
include( cmake/LinuxDistribution.cmake )
include( cmake/build_rules.cmake )
include( cmake/boost.cmake)
include( cmake/find_strus_package.cmake )

find_strus_package( core )
find_strus_package( analyzer )
find_strus_package( module )
find_strus_package( rpc )

set( CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" )
set( CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON )
configure_file( "${PROJECT_SOURCE_DIR}/include/private/version.hpp.in"  "${PROJECT_SOURCE_DIR}/include/private/version.hpp"  @ONLY )

set( STRUS_GETTEXT_PACKAGE "strus-dom" )	#... 1st parameter of bindtextdomain(...)
set( STRUS_GETTEXT_LOCALEDIR "" )			#... 2nd parameter of bindtextdomain(...)
configure_file( "${PROJECT_SOURCE_DIR}/src/utils/internationalization.cpp.in"  "${PROJECT_SOURCE_DIR}/src/utils/internationalization.cpp"  @ONLY )
configure_file( "${PROJECT_SOURCE_DIR}/dist/obs/deploy_to_obs.sh.in"  "${PROJECT_SOURCE_DIR}/dist/obs/deploy_to_obs.sh"  @ONLY )
configure_file( "${PROJECT_SOURCE_DIR}/dist/archlinux/PKGBUILD.in"  "${PROJECT_SOURCE_DIR}/dist/archlinux/PKGBUILD"  @ONLY )
configure_file( "${PROJECT_SOURCE_DIR}/dist/obs/strusutilities.dsc.in"  "${PROJECT_SOURCE_DIR}/dist/obs/strusutilities.dsc"  @ONLY )
configure_file( "${PROJECT_SOURCE_DIR}/dist/obs/strusutilities-Debian_7.0.dsc.in"  "${PROJECT_SOURCE_DIR}/dist/obs/strusutilities-Debian_7.0.dsc"  @ONLY )
configure_file( "${PROJECT_SOURCE_DIR}/dist/obs/strusutilities-xUbuntu_12.04.dsc.in"  "${PROJECT_SOURCE_DIR}/dist/obs/strusutilities-xUbuntu_12.04.dsc"  @ONLY )
configure_file( "${PROJECT_SOURCE_DIR}/dist/redhat/strusutilities.spec.in"  "${PROJECT_SOURCE_DIR}/dist/redhat/strusutilities.spec"  @ONLY )

enable_testing()

# Subdirectories:
add_subdirectory( src )
  
include( cmake/report_build_settings.cmake )

# Install header files:
install(DIRECTORY include/strus
          DESTINATION include
          FILES_MATCHING PATTERN "*.hpp")

include( cmake/standard_targets.cmake )

