cmake_minimum_required(VERSION 2.8 FATAL_ERROR )

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set( source_files
	strusMergeMarkup.cpp
)

include_directories(
	    "${Intl_INCLUDE_DIRS}"
            ${Boost_INCLUDE_DIRS}
	    "${UTILS_INCLUDE_DIRS}"
	    "${PROGRAM_INCLUDE_DIRS}"
	    "${strusbase_INCLUDE_DIRS}"
	    "${strusanalyzer_INCLUDE_DIRS}"
	    "${strus_INCLUDE_DIRS}"
)
link_directories( 
	   "${UTILS_LIBRARY_DIRS}" 
	   "${PROGRAM_LIBRARY_DIRS}"
	   ${Boost_LIBRARY_DIRS} 
	   "${strusbase_LIBRARY_DIRS}"
	   "${strusanalyzer_LIBRARY_DIRS}"
	   "${strus_LIBRARY_DIRS}"
)

# ------------------------------
# PROGRAMS
# ------------------------------
add_cppcheck( strusMergeMarkup ${source_files} )

add_executable( strusMergeMarkup ${source_files} )
target_link_libraries( strusMergeMarkup strusutilities_private_utils strus_doctree strus_filecrawler strus_filelocator strus_textproc strus_detector_std strus_base strus_error ${Intl_LIBRARIES} )

# FreeBSD needs kernel data access library for libuv (-libkvm)
find_library( LIBKVM_LIBRARIES kvm )
if(LIBKVM_LIBRARIES)
	target_link_libraries( strusMergeMarkup ${LIBKVM_LIBRARIES} )
endif()

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusMergeMarkup
	   RUNTIME DESTINATION bin )

