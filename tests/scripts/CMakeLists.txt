cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

add_test( TestFramework1 ./runTest.sh testTestFramework1 )
add_test( TestFramework2 ./runTest.sh testTestFramework2 )
add_test( testPatternMatch1 ./runTest.sh testPatternMatch1 )
add_test( testPatternMatch2 ./runTest.sh testPatternMatch2 )
add_test( testPatternMatch3 ./runTest.sh testPatternMatch3 )


