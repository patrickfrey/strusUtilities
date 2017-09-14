cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

include_directories(
	    "${Intl_INCLUDE_DIRS}"
            "${Boost_INCLUDE_DIRS}"
	    "${strusbase_INCLUDE_DIRS}"
	    "${PROJECT_SOURCE_DIR}/include"
)
link_directories( 
	   "${strusbase_LIBRARY_DIRS}"
            "${Boost_LIBRARY_DIRS}"
	    "${CMAKE_CURRENT_BINARY_DIR}/../../src/utils" 
)

add_executable( execTest execTest.cpp )
target_link_libraries( execTest strus_base ${Intl_LIBRARIES})

IF( UNIX )
IF (WITH_STRUS_PATTERN STREQUAL "YES")
add_test( TestTest execTest )
add_test( TestPatternMatch1  execTest testPatternMatch1 ${PROJECT_SOURCE_DIR} )
add_test( TestPatternMatch2  execTest  testPatternMatch2 ${PROJECT_SOURCE_DIR} )
add_test( TestPatternMatch3  execTest  testPatternMatch3 ${PROJECT_SOURCE_DIR} )
add_test( TestPatternMatch4  execTest  testPatternMatch4 ${PROJECT_SOURCE_DIR} )
add_test( TestPatternMatchNamesEditDist execTest   testPatternMatchNamesEditDist ${PROJECT_SOURCE_DIR} )
add_test( TestPatternMatchEditDist2 execTest   testPatternMatchEditDist2 ${PROJECT_SOURCE_DIR} )
add_test( TestPatternMatchNamesInputFromFile execTest  testPatternMatchNamesInputFromFile ${PROJECT_SOURCE_DIR} )
add_test( TestPatternMatchNamesTextInputFromFile  execTest   testPatternMatchNamesTextInputFromFile ${PROJECT_SOURCE_DIR} )
ENDIF (WITH_STRUS_PATTERN STREQUAL "YES")

add_test( TestAnalyzeSubSegmenter1 execTest   testAnalyzeSubSegmenter1 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzeSubSegmenter2 execTest   testAnalyzeSubSegmenter2 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzeSubSegmenter3 execTest   testAnalyzeSubSegmenter3 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzeBase1 execTest   testAnalyzeBase1 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzeJson1 execTest   testAnalyzeJson1 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzeJson2 execTest   testAnalyzeJson2 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzePlain1 execTest   testAnalyzePlain1 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzeTsv1 execTest   testAnalyzeTsv1 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzeConfig1 execTest   testAnalyzeConfig1 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzeConfig2 execTest   testAnalyzeConfig2 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzeWithDocType1 execTest   testAnalyzeWithDocType1 ${PROJECT_SOURCE_DIR} )
add_test( TestAnalyzeBindPos1 execTest   testAnalyzeBindPos1 ${PROJECT_SOURCE_DIR} )
add_test( TestInsertWithDocType1 execTest   testInsertWithDocType1 ${PROJECT_SOURCE_DIR} )
add_test( TestInsertBase1 execTest   testInsertBase1 ${PROJECT_SOURCE_DIR} )
add_test( TestSimpleQuery1  execTest   testSimpleQuery1 ${PROJECT_SOURCE_DIR} )
add_test( TestSimpleQuery2  execTest   testSimpleQuery2 ${PROJECT_SOURCE_DIR} )
add_test( TestQueryWithRestriction1 execTest   testQueryWithRestriction1 ${PROJECT_SOURCE_DIR} )
add_test( TestQueryWithFormula1 execTest    testQueryWithFormula1 ${PROJECT_SOURCE_DIR} )
add_test( TestSummarization1 execTest   testSummarization1 ${PROJECT_SOURCE_DIR} )
add_test( TestUpdateCalcStats execTest   testUpdateCalcStats1 ${PROJECT_SOURCE_DIR} )
ENDIF( UNIX )

