cmake_minimum_required(VERSION 2.8 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set( source_files
	fileCrawler.cpp
	libstrus_filecrawler.cpp
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
	${Boost_LIBRARY_DIRS}
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
add_cppcheck( strus_filecrawler  ${source_files} )

add_library( strus_filecrawler SHARED ${source_files})
target_link_libraries( strus_filecrawler  strusutilities_private_utils  strus_base  strus_error ${Boost_LIBRARIES} ${Intl_LIBRARIES})

set_target_properties(
    strus_filecrawler
    PROPERTIES
    DEBUG_POSTFIX "${CMAKE_DEBUG_POSTFIX}"
    SOVERSION "${STRUS_MAJOR_VERSION}.${STRUS_MINOR_VERSION}"
    VERSION ${STRUS_VERSION}
)

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strus_filecrawler
           LIBRARY DESTINATION ${LIB_INSTALL_DIR}/strus )

