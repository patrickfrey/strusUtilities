cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	strusAnalyze.cpp
)

include_directories( "${Boost_INCLUDE_DIRS}"  "${PROJECT_SOURCE_DIR}/include"  "${strusanalyzer_INCLUDE_DIRS}"  "${strus_INCLUDE_DIRS}" )
link_directories( "${Boost_LIBRARY_DIRS}" "${PROJECT_SOURCE_DIR}/program"  "${strusanalyzer_LIBRARY_DIRS}"  "${strus_LIBRARY_DIRS}"  )


# ------------------------------
# PROGRAMS
# ------------------------------
add_executable( strusAnalyze ${source_files} )
target_link_libraries( strusAnalyze ${strusAnalyzer_LIBRARIES} ${strus_LIBRARIES} strus_program boost_system)


# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusAnalyze
	   RUNTIME DESTINATION bin )

