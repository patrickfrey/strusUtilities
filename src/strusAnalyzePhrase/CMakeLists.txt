cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	strusAnalyzePhrase.cpp
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
add_executable( strusAnalyzePhrase ${source_files} )
target_link_libraries( strusAnalyzePhrase  strus_private_utils  ${strusmodule_LIBRARIES}  ${strusanalyzer_LIBRARIES} ${strus_LIBRARIES} strus_program )


# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusAnalyzePhrase
	   RUNTIME DESTINATION bin )

