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
add_executable( strusPatternMatcher  strusPatternMatcher.cpp )
target_link_libraries( strusPatternMatcher  strus_private_utils strus_private_utils  ${strusmodule_LIBRARIES} strus_program strus_base strus_error strus_markup_std strus_module strus_rpc_client strus_rpc_client_socket ${Boost_LIBRARIES} ${Intl_LIBRARIES})

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusPatternMatcher
	   RUNTIME DESTINATION bin )
