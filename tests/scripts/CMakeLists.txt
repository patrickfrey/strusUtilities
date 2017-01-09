cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

add_test( TestFramework1    ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testTestFramework1 )
add_test( TestFramework2    ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testTestFramework2 )

IF (WITH_STRUS_PATTERN STREQUAL "YES")
add_test( TestPatternMatch1  ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testPatternMatch1 )
add_test( TestPatternMatch2  ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testPatternMatch2 )
add_test( TestPatternMatch3  ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testPatternMatch3 )
ENDIF (WITH_STRUS_PATTERN STREQUAL "YES")



