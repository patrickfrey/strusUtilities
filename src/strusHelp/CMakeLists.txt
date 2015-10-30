cmake_minimum_required(VERSION 2.6 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
include_directories(
            "${Boost_INCLUDE_DIRS}"
	    "${PROJECT_SOURCE_DIR}/include"
	    "${strus_INCLUDE_DIRS}"
	    "${strusanalyzer_INCLUDE_DIRS}"
	    "${strusmodule_INCLUDE_DIRS}"
	    "${strusrpc_INCLUDE_DIRS}"	    
)
link_directories( 
	   "${PROJECT_SOURCE_DIR}/utils" 
	   "${PROJECT_SOURCE_DIR}/program" 
	   "${strus_LIBRARY_DIRS}"
	   "${strusanalyzer_LIBRARY_DIRS}"
	   "${strusmodule_LIBRARY_DIRS}"
	   "${strusrpc_LIBRARY_DIRS}"
)


# ------------------------------
# PROGRAMS
# ------------------------------
add_executable( strusHelp  strusHelp.cpp )
target_link_libraries( strusHelp strus_private_utils strus_program strus_utils strus_error strus_module strus_rpc_client strus_rpc_client_socket )

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusHelp
	   RUNTIME DESTINATION bin )

