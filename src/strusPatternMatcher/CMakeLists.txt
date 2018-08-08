cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
include_directories(
	${Boost_INCLUDE_DIRS}
	"${Intl_INCLUDE_DIRS}"
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
	${PROGRAM_LIBRARY_DIRS}
	${Boost_LIBRARY_DIRS}
   	"${strusbase_LIBRARY_DIRS}"
	"${strusanalyzer_LIBRARY_DIRS}"
	"${strustrace_LIBRARY_DIRS}"
	"${strusmodule_LIBRARY_DIRS}"
	"${strusrpc_LIBRARY_DIRS}"
)

# -------------------------------------------
# PROGRAMS
# -------------------------------------------
add_cppcheck( strusPatternMatcher  strusPatternMatcher.cpp )

add_executable( strusPatternMatcher  strusPatternMatcher.cpp )
target_link_libraries( strusPatternMatcher strusutilities_private_utils  ${strusmodule_LIBRARIES} strus_program strus_pattern_resultformat strus_error strus_markup_std strus_module strus_rpc_client strus_rpc_client_socket strus_base ${Boost_LIBRARIES} ${Intl_LIBRARIES})

# FreeBSD needs kernel data access library for libuv (-libkvm)
find_library( LIBKVM_LIBRARIES kvm )
if(LIBKVM_LIBRARIES)
	target_link_libraries( strusPatternMatcher ${LIBKVM_LIBRARIES} )
endif()

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusPatternMatcher
	   RUNTIME DESTINATION bin )

