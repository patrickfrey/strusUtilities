cmake_minimum_required(VERSION 2.6 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	strusCheckStorage.cpp
)

include_directories(
 		"${Intl_INCLUDE_DIRS}"
            "${Boost_INCLUDE_DIRS}"
	    "${PROJECT_SOURCE_DIR}/include"
	    "${strus_INCLUDE_DIRS}"
	    "${strusmodule_INCLUDE_DIRS}"
	    "${strusrpc_INCLUDE_DIRS}"	    
)
link_directories( 
	   "${PROJECT_SOURCE_DIR}/utils" 
	   "${PROJECT_SOURCE_DIR}/program" 
	   "${strus_LIBRARY_DIRS}"
	   "${strusmodule_LIBRARY_DIRS}"
	   "${strusrpc_LIBRARY_DIRS}"
)

# ------------------------------
# PROGRAMS
# ------------------------------
add_executable( strusCheckStorage ${source_files} )
target_link_libraries( strusCheckStorage  strus_private_utils  strus_utils strus_error strus_module strus_rpc_client strus_rpc_client_socket ${Intl_LIBRARIES})

# FreeBSD needs kernel data access library for libuv (-libkvm)
find_library( LIBKVM_LIBRARIES kvm )
if(LIBKVM_LIBRARIES)
	target_link_libraries( strusCheckStorage ${LIBKVM_LIBRARIES} )
endif()

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusCheckStorage
	   RUNTIME DESTINATION bin )

