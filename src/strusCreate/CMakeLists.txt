cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

# --------------------------------------
# SOURCES AND INCLUDES
# --------------------------------------
set(source_files
	strusCreate.cpp
)

include_directories( "${Boost_INCLUDE_DIRS}" "${PROJECT_SOURCE_DIR}/include"  "${strus_INCLUDE_DIRS}" )
link_directories( "${Boost_LIBRARY_DIRS}" "${PROJECT_SOURCE_DIR}/program"  "${strus_LIBRARY_DIRS}" )


# ------------------------------
# PROGRAMS
# ------------------------------
add_executable( strusCreate ${source_files} )
target_link_libraries( strusCreate ${strus_LIBRARIES} boost_system)


# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusCreate
	   RUNTIME DESTINATION bin )

