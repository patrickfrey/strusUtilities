cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	lexems.cpp
	programLoader.cpp
)

include_directories(
            "${Boost_INCLUDE_DIRS}"
	    "${PROJECT_SOURCE_DIR}/include"
	    "${strus_INCLUDE_DIRS}"
	    "${strusanalyzer_INCLUDE_DIRS}"
	    "${strusmodule_INCLUDE_DIRS}" )
link_directories( 
           "${Boost_LIBRARY_DIRS}"
	   "${PROJECT_SOURCE_DIR}/program" 
	   "${PROJECT_SOURCE_DIR}/utils" 
	   "${strus_LIBRARY_DIRS}"
	   "${strusanalyzer_LIBRARY_DIRS}"
	   "${strusmodule_LIBRARY_DIRS}" )


# -------------------------------------------
# LIBRARIES
# -------------------------------------------
add_library( strus_program SHARED ${source_files} )
target_link_libraries( strus_program strus_private_utils "${Boost_LIBRARIES}"  "${strus_LIBRARIES}" "${strusanalyzer_LIBRARIES}"  "${strusmodule_LIBRARIES}" )

set_target_properties(
    strus_program
    PROPERTIES
    DEBUG_POSTFIX "_d"
    SOVERSION "${STRUS_MAJOR_VERSION}.${STRUS_MINOR_VERSION}"
    VERSION ${STRUS_VERSION}
)

# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strus_program
           LIBRARY DESTINATION lib/strus )

