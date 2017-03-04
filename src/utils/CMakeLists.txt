cmake_minimum_required(VERSION 2.8 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	utils.cpp
	internationalization.cpp
	traceUtils.cpp
	analyzerMap.cpp
)

include_directories(
	"${Boost_INCLUDE_DIRS}"
	"${PROJECT_SOURCE_DIR}/include"
	"${strusbase_INCLUDE_DIRS}"
	"${strus_INCLUDE_DIRS}"
	"${strusanalyzer_INCLUDE_DIRS}"
	"${strustrace_INCLUDE_DIRS}"
	"${strusmodule_INCLUDE_DIRS}"
)
link_directories(
	"${PROJECT_SOURCE_DIR}/utils" 
	"${strusbase_LIBRARY_DIRS}"
	"${strus_LIBRARY_DIRS}"
	"${strusanalyzer_LIBRARY_DIRS}"
)

# -------------------------------------------
# UTILS LIBRARY
# Library with some helper functions used by strus libraries and programs but not part of the public interface
# -------------------------------------------
add_library( strusutilities_private_utils STATIC  ${source_files} )
target_link_libraries( strusutilities_private_utils  "${Boost_LIBRARIES}" )



