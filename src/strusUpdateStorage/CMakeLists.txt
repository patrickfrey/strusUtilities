cmake_minimum_required(VERSION 2.6 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	strusUpdateStorage.cpp
)

include_directories(
            "${Boost_INCLUDE_DIRS}"
	    "${PROJECT_SOURCE_DIR}/include"
	    "${strus_INCLUDE_DIRS}"
	    "${strusanalyzer_INCLUDE_DIRS}"
	    "${strusmodule_INCLUDE_DIRS}"
	    "${strusrpc_INCLUDE_DIRS}"
)
link_directories( 
	   "${PROJECT_SOURCE_DIR}/program" 
	   "${PROJECT_SOURCE_DIR}/utils" 
	   "${strus_LIBRARY_DIRS}"
	   "${strusanalyzer_LIBRARY_DIRS}"
	   "${strusmodule_LIBRARY_DIRS}"
	   "${strusrpc_LIBRARY_DIRS}"
)


# ------------------------------
# PROGRAMS
# ------------------------------
add_executable( strusUpdateStorage ${source_files} )
target_link_libraries( strusUpdateStorage strus_private_utils ${strus_LIBRARIES} ${strusmodule_LIBRARIES} ${strusrpc_LIBRARIES} strus_program )


# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusUpdateStorage
	   RUNTIME DESTINATION bin )

