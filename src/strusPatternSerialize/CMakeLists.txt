cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
include_directories(
	"${Boost_INCLUDE_DIRS}"
	"${Intl_INCLUDE_DIRS}"
	"${PROJECT_SOURCE_DIR}/include"
	"${strusbase_INCLUDE_DIRS}"
	"${strusanalyzer_INCLUDE_DIRS}"
	"${strustrace_INCLUDE_DIRS}"
	"${strusmodule_INCLUDE_DIRS}"
	"${strusrpc_INCLUDE_DIRS}"	    
)

link_directories(
	"${PROJECT_SOURCE_DIR}/utils"
	"${Boost_LIBRARY_DIRS}"
   	"${strusbase_LIBRARY_DIRS}"
	"${strusanalyzer_LIBRARY_DIRS}"
	"${strustrace_LIBRARY_DIRS}"
	"${strusmodule_LIBRARY_DIRS}"
	"${strusrpc_LIBRARY_DIRS}"
)

# -------------------------------------------
# PROGRAMS
# -------------------------------------------
add_executable( strusPatternSerialize  strusPatternSerialize.cpp )
target_link_libraries( strusPatternSerialize  strus_private_utils strus_private_utils  ${strusanalyzer_LIBRARIES}  ${strusmodule_LIBRARIES} strus_program strus_base strus_error  strus_pattern_serialize strus_module strus_rpc_client strus_rpc_client_socket ${Boost_LIBRARIES} ${Intl_LIBRARIES})

# FreeBSD needs kernel data access library for libuv (-libkvm)
find_library( LIBKVM_LIBRARIES kvm )
if(LIBKVM_LIBRARIES)
	target_link_libraries( strusPatternSerialize ${LIBKVM_LIBRARIES} )
endif()

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusPatternSerialize
	   RUNTIME DESTINATION bin )

