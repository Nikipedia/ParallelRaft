# CMakeList.txt : CMake project for Implementation2, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)
project(Implementation2)
# Add source to this project's executable.
add_executable (Implementation2 "InputThread.cpp" "IOPolling.cpp" "BlockingCollection.h")

# TODO: Add tests and install targets if needed.
