cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	strusCheckStorage.cpp
)

include_directories(
            "${Boost_INCLUDE_DIRS}"
	    "${PROJECT_SOURCE_DIR}/include"
	    "${strus_INCLUDE_DIRS}"
	    "${strusmodule_INCLUDE_DIRS}" )
link_directories( 
           "${Boost_LIBRARY_DIRS}"
	   "${PROJECT_SOURCE_DIR}/program" 
	   "${strus_LIBRARY_DIRS}"
	   "${strusmodule_LIBRARY_DIRS}" )

# ------------------------------
# PROGRAMS
# ------------------------------
add_executable( strusCheckStorage ${source_files} )
target_link_libraries( strusCheckStorage ${strusmodule_LIBRARIES} ${strus_LIBRARIES} boost_system)


# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusCheckStorage
	   RUNTIME DESTINATION bin )

