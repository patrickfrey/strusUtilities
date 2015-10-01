cmake_minimum_required(VERSION 2.6 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
include_directories(
	"${Boost_INCLUDE_DIRS}"
	"${PROJECT_SOURCE_DIR}/include" )
link_directories(
	"${PROJECT_SOURCE_DIR}/utils" 
	"${PROJECT_SOURCE_DIR}/utils" )


# -------------------------------------------
# UTILS LIBRARY
# Library with some helper functions used by strus libraries and programs but not part of the public interface
# -------------------------------------------
add_library( strus_private_utils STATIC utils.cpp inputStream.cpp internationalization.cpp )
target_link_libraries( strus_private_utils  "${Boost_LIBRARIES}" )



