cmake_minimum_required(VERSION 2.8 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set( source_files
	strusUpdateStorage.cpp
)

include_directories(
	"${Intl_INCLUDE_DIRS}"
	${Boost_INCLUDE_DIRS}
	"${UTILS_INCLUDE_DIRS}"
	"${PROGRAM_INCLUDE_DIRS}"
	"${strusbase_INCLUDE_DIRS}"
	"${strus_INCLUDE_DIRS}"
	"${strusanalyzer_INCLUDE_DIRS}"
	"${strustrace_INCLUDE_DIRS}"
	"${strusmodule_INCLUDE_DIRS}"
	"${strusrpc_INCLUDE_DIRS}"
)
link_directories( 
	"${UTILS_LIBRARY_DIRS}" 
	"${PROGRAM_LIBRARY_DIRS}"
	${Boost_LIBRARY_DIRS}  
	"${strusbase_LIBRARY_DIRS}"
	"${strus_LIBRARY_DIRS}"
	"${strusanalyzer_LIBRARY_DIRS}"
	"${strustrace_LIBRARY_DIRS}"
	"${strusmodule_LIBRARY_DIRS}"
	"${strusrpc_LIBRARY_DIRS}"
)


# ------------------------------
# PROGRAMS
# ------------------------------
add_cppcheck( strusUpdateStorage  ${source_files} )

add_executable( strusUpdateStorage ${source_files} )
target_link_libraries( strusUpdateStorage strus_storage_objbuild strus_storage_prgload_std strusutilities_private_utils strus_base strus_error strus_module strus_rpc_client strus_rpc_client_socket ${Intl_LIBRARIES})

# FreeBSD needs kernel data access library for libuv (-libkvm)
find_library( LIBKVM_LIBRARIES kvm )
if(LIBKVM_LIBRARIES)
	target_link_libraries( strusUpdateStorage ${LIBKVM_LIBRARIES} )
endif()

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusUpdateStorage
	   RUNTIME DESTINATION bin )

