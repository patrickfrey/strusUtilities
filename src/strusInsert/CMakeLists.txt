cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

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

include_directories( "${Boost_INCLUDE_DIRS}"  "${PROJECT_SOURCE_DIR}/include"  "${strus_INCLUDE_DIRS}"  "${strusanalyzer_INCLUDE_DIRS}" )
link_directories( "${Boost_LIBRARY_DIRS}" "${PROJECT_SOURCE_DIR}/program"  "${strus_LIBRARY_DIRS}"  "${strusanalyzer_LIBRARY_DIRS}" )


# ------------------------------
# PROGRAMS
# ------------------------------
add_library( local_objs_static STATIC ${source_files})

add_executable( strusInsert  strusInsert.cpp )
target_link_libraries( strusInsert local_objs_static strus_program ${strus_LIBRARIES} ${strusanalyzer_LIBRARIES} ${Boost_LIBRARIES})

add_executable( strusCheckInsert  strusCheckInsert.cpp )
target_link_libraries( strusCheckInsert local_objs_static strus_program ${strus_LIBRARIES} ${strusanalyzer_LIBRARIES} ${Boost_LIBRARIES})

add_executable( strusGenerateKeyMap strusGenerateKeyMap.cpp )
target_link_libraries( strusGenerateKeyMap local_objs_static ${strus_LIBRARIES} ${strusanalyzer_LIBRARIES} ${Boost_LIBRARIES})


# ------------------------------
# INSTALLATION
# ------------------------------
install( TARGETS strusInsert
	   RUNTIME DESTINATION bin )

install( TARGETS strusCheckInsert
	   RUNTIME DESTINATION bin )

install( TARGETS strusGenerateKeyMap
	   RUNTIME DESTINATION bin )

