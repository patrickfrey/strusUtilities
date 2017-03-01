cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

add_test( TestFramework1    ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testTestFramework1 )
add_test( TestFramework2    ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testTestFramework2 )

IF (WITH_STRUS_PATTERN STREQUAL "YES")
add_test( TestPatternMatch1  ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testPatternMatch1 )
add_test( TestPatternMatch2  ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testPatternMatch2 )
add_test( TestPatternMatch3  ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testPatternMatch3 )
add_test( TestPatternMatch4  ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testPatternMatch4 )
ENDIF (WITH_STRUS_PATTERN STREQUAL "YES")

add_test( TestAnalyzeSubSegmenter1 ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh  testAnalyzeSubSegmenter1 )
add_test( TestAnalyzeSubSegmenter2 ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh  testAnalyzeSubSegmenter2 )
add_test( TestAnalyzeBase1 ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh  testAnalyzeBase1 )
add_test( TestInsertBase1 ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh  testInsertBase1 )
add_test( TestSummarization1 ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh  testSummarization1 )



