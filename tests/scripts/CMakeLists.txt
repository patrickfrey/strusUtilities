cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

include_directories(
	    "${Intl_INCLUDE_DIRS}"
            ${Boost_INCLUDE_DIRS}
	    "${strusbase_INCLUDE_DIRS}"
	    "${UTILS_INCLUDE_DIRS}"
)
link_directories( 
	   "${strusbase_LIBRARY_DIRS}"
            ${Boost_LIBRARY_DIRS}
	    "${UTILS_INCLUDE_DIRS}" 
)

add_executable( execTest execTest.cpp )
target_link_libraries( execTest strus_base ${Intl_LIBRARIES})

IF( UNIX )
macro( add_utilities_test NAME )
    add_test( Test${NAME} execTest test${NAME} ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/../..  STRUS_MODULE_PATH=${STRUS_MODULE_PATH} )
endmacro( add_utilities_test )

add_utilities_test( InsertWikipedia1 )

IF (WITH_STRUS_PATTERN STREQUAL "YES")
add_test( TestTest execTest )
add_utilities_test( PatternMatch1 )
add_utilities_test( PatternMatch2 )
add_utilities_test( PatternMatch3 )
add_utilities_test( PatternMatch4 )
add_utilities_test( PatternMatchNamesEditDist )
add_utilities_test( PatternMatchEditDist2 )
add_utilities_test( PatternMatchNamesInputFromFile )
add_utilities_test( PatternMatchNamesTextInputFromFile )
add_utilities_test( PatternMatchNamesEditDist )
add_utilities_test( PatternMatchNamesEditDist )
add_utilities_test( PatternMatchNamesEditDist )
add_utilities_test( PatternMatchNamesEditDist )
add_utilities_test( PatternMatchFormatString1 )
add_utilities_test( PatternMatchVariableSequence )
add_utilities_test( AnalyzePatternMatch1 )
ENDIF (WITH_STRUS_PATTERN STREQUAL "YES")

add_utilities_test( AnalyzeSubSegmenter1 )
add_utilities_test( AnalyzeSubSegmenter2 )
add_utilities_test( AnalyzeSubSegmenter3 )
add_utilities_test( AnalyzeBase1 )
add_utilities_test( AnalyzeJson1 )
add_utilities_test( AnalyzeJson2 )
add_utilities_test( AnalyzePlain1 )
add_utilities_test( AnalyzeTsv1 )
add_utilities_test( AnalyzeConfig1 )
add_utilities_test( AnalyzeConfig2 )
add_utilities_test( AnalyzeWithDocType1 )
add_utilities_test( AnalyzeBindPos1 )
add_utilities_test( AnalyzeFieldsAndStructures )
add_utilities_test( InsertWithDocType1 )
add_utilities_test( InsertBase1 )
add_utilities_test( SimpleQuery1 )
add_utilities_test( SimpleQuery2 )
add_utilities_test( QueryWithRestriction1 )
add_utilities_test( QueryWithFormula1 )
add_utilities_test( Summarization1 )
add_utilities_test( UpdateCalcStats1 )
add_utilities_test( PosTagger1 )
add_utilities_test( MarkupDocumentTags )
ENDIF( UNIX )

