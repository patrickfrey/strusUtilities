cmake_minimum_required(VERSION 2.8 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	checkInsertProcessor.cpp
	commitQueue.cpp
	fileCrawler.cpp
	insertProcessor.cpp
	keyMapGenProcessor.cpp
)

include_directories(
	"${Intl_INCLUDE_DIRS}"
	"${Boost_INCLUDE_DIRS}"
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
add_library( local_objs_static STATIC ${source_files})
set_property( TARGET local_objs_static PROPERTY POSITION_INDEPENDENT_CODE TRUE )
set_property( TARGET local_objs_static PROPERTY POSITION_INDEPENDENT_CODE TRUE )

add_executable( strusInsert  strusInsert.cpp )
target_link_libraries( strusInsert  strus_storage_objbuild strusutilities_private_utils  local_objs_static strus_program strus_base strus_error strus_module strus_rpc_client strus_rpc_client_socket ${Boost_LIBRARIES} ${Intl_LIBRARIES})

# FreeBSD needs kernel data access library for libuv (-libkvm)
find_library( LIBKVM_LIBRARIES kvm )
if(LIBKVM_LIBRARIES)
	target_link_libraries( strusInsert ${LIBKVM_LIBRARIES} )
endif()

add_executable( strusCheckInsert  strusCheckInsert.cpp )
target_link_libraries( strusCheckInsert  strus_storage_objbuild strusutilities_private_utils local_objs_static strus_program  strus_base strus_error strus_module strus_rpc_client strus_rpc_client_socket ${Boost_LIBRARIES} ${Intl_LIBRARIES})

if(LIBKVM_LIBRARIES)
	target_link_libraries( strusCheckInsert ${LIBKVM_LIBRARIES} )
endif()

add_executable( strusGenerateKeyMap strusGenerateKeyMap.cpp )
target_link_libraries( strusGenerateKeyMap  strusutilities_private_utils local_objs_static  strus_program strus_base strus_error strus_module strus_rpc_client strus_rpc_client_socket ${Boost_LIBRARIES} ${Intl_LIBRARIES})

if(LIBKVM_LIBRARIES)
	target_link_libraries( strusGenerateKeyMap ${LIBKVM_LIBRARIES} )
endif()

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusInsert
	   RUNTIME DESTINATION bin )

install( TARGETS strusCheckInsert
	   RUNTIME DESTINATION bin )

install( TARGETS strusGenerateKeyMap
	   RUNTIME DESTINATION bin )

