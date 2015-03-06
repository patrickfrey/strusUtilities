cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	strusAnalyze.cpp
)

include_directories(
            "${Boost_INCLUDE_DIRS}"
	    "${PROJECT_SOURCE_DIR}/include"
	    "${strus_INCLUDE_DIRS}"
	    "${strusanalyzer_INCLUDE_DIRS}"
	    "${strusmodule_INCLUDE_DIRS}" )
link_directories( 
           "${Boost_LIBRARY_DIRS}"
	   "${PROJECT_SOURCE_DIR}/program" 
	   "${strus_LIBRARY_DIRS}"
	   "${strusanalyzer_LIBRARY_DIRS}"
	   "${strusmodule_LIBRARY_DIRS}" )

# ------------------------------
# PROGRAMS
# ------------------------------
add_executable( strusAnalyze ${source_files} )
target_link_libraries( strusAnalyze  ${strusmodule_LIBRARIES}  ${strusanalyzer_LIBRARIES} ${strus_LIBRARIES} strus_program boost_system)


# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusAnalyze
	   RUNTIME DESTINATION bin )

