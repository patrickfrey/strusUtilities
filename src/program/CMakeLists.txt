cmake_minimum_required(VERSION 2.8 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set( source_files
	lexems.cpp
	errorPosition.cpp
	termExpression.cpp
	metadataExpression.cpp
	patternMatchProgramParser.cpp
	programLoader.cpp
)

include_directories(
	"${Intl_INCLUDE_DIRS}"
	${Boost_INCLUDE_DIRS}
	"${UTILS_INCLUDE_DIRS}"
	"${strusbase_INCLUDE_DIRS}"
	"${strus_INCLUDE_DIRS}"
	"${strusanalyzer_INCLUDE_DIRS}"
	"${strusmodule_INCLUDE_DIRS}"
	"${strusrpc_INCLUDE_DIRS}"	    
)
link_directories( 
	${Boost_LIBRARY_DIRS}
	"${UTILS_LIBRARY_DIRS}" 
	"${strusbase_LIBRARY_DIRS}"
	"${strus_LIBRARY_DIRS}"
	"${strusanalyzer_LIBRARY_DIRS}"
	"${strusmodule_LIBRARY_DIRS}"
	"${strusrpc_LIBRARY_DIRS}"
)

# -------------------------------------------
# LIBRARIES
# -------------------------------------------
add_cppcheck( strus_program  ${source_files} )

add_library( strus_program SHARED ${source_files} )
target_link_libraries( strus_program strusutilities_private_utils "${Boost_LIBRARIES}"  strus_base strus_error strus_module strus_pattern_serialize strus_rpc_client strus_rpc_client_socket ${Intl_LIBRARIES} )

set_target_properties(
    strus_program
    PROPERTIES
    DEBUG_POSTFIX "${CMAKE_DEBUG_POSTFIX}"
    SOVERSION "${STRUS_MAJOR_VERSION}.${STRUS_MINOR_VERSION}"
    VERSION ${STRUS_VERSION}
)

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strus_program
           LIBRARY DESTINATION ${LIB_INSTALL_DIR}/strus )

