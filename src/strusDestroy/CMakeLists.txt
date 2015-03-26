cmake_minimum_required(VERSION 2.6 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	strusDestroy.cpp
)

include_directories(
            "${Boost_INCLUDE_DIRS}"
	    "${PROJECT_SOURCE_DIR}/include"
	    "${strus_INCLUDE_DIRS}"
	    "${strusanalyzer_INCLUDE_DIRS}"
	    "${strusmodule_INCLUDE_DIRS}" )
link_directories( 
	   "${PROJECT_SOURCE_DIR}/utils" 
	   "${PROJECT_SOURCE_DIR}/program" 
	   "${strus_LIBRARY_DIRS}"
	   "${strusanalyzer_LIBRARY_DIRS}"
	   "${strusmodule_LIBRARY_DIRS}" )


# ------------------------------
# PROGRAMS
# ------------------------------
add_executable( strusDestroy ${source_files} )
target_link_libraries( strusDestroy  strus_private_utils ${strusmodule_LIBRARIES} ${strus_LIBRARIES} )


# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusDestroy
	   RUNTIME DESTINATION bin )

