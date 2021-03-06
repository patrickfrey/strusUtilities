cmake_minimum_required(VERSION 2.8 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set( source_files
	${CMAKE_CURRENT_BINARY_DIR}/internationalization.cpp
	traceUtils.cpp
	documentAnalyzer.cpp
	parseFunctionDef.cpp
)

include_directories(
	"${Intl_INCLUDE_DIRS}"
	${Boost_INCLUDE_DIRS}
	${UTILS_INCLUDE_DIRS}
	"${strusbase_INCLUDE_DIRS}"
	"${strus_INCLUDE_DIRS}"
	"${strusanalyzer_INCLUDE_DIRS}"
	"${strustrace_INCLUDE_DIRS}"
	"${strusmodule_INCLUDE_DIRS}"
)
link_directories(
	${Boost_LIBRARY_DIRS}
	"${strusbase_LIBRARY_DIRS}"
	"${strus_LIBRARY_DIRS}"
	"${strusanalyzer_LIBRARY_DIRS}"
)

# -------------------------------------------
# UTILS LIBRARY
# Library with some helper functions used by strus libraries and programs but not part of the public interface
# -------------------------------------------
add_cppcheck( strusutilities_private_utils  ${source_files} )

add_library( strusutilities_private_utils STATIC  ${source_files} )
target_link_libraries( strusutilities_private_utils  strus_analyzer_prgload_std ${Boost_LIBRARIES} ${Intl_LIBRARIES} )
set_property( TARGET strusutilities_private_utils PROPERTY POSITION_INDEPENDENT_CODE TRUE )



