cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

add_test( TestFramework1    ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testTestFramework1 )
add_test( TestFramework2    ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testTestFramework2 )
# DISABLED  add_test( TestPatternMatch1  ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testPatternMatch1 )
# DISABLED  add_test( TestPatternMatch2  ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testPatternMatch2 )
# DISABLED  add_test( TestPatternMatch3  ${PROJECT_SOURCE_DIR}/tests/scripts/runTest.sh testPatternMatch3 )


