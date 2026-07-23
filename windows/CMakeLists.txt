cmake_minimum_required(VERSION 3.25)
project(MtpTouchpadReceiver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mtp1 STATIC
    receiver/Mtp1.cpp
    receiver/TouchSession.cpp
)
target_include_directories(mtp1 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/receiver
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
)
target_compile_definitions(mtp1 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(mtp1 PRIVATE /W4 /WX /permissive-)
endif()

add_executable(mtp-receiver receiver/main.cpp)
target_link_libraries(mtp-receiver PRIVATE mtp1 ws2_32)
if(MSVC)
    target_compile_options(mtp-receiver PRIVATE /W4 /WX /permissive-)
endif()

enable_testing()
add_executable(mtp-tests tests/MtpTests.cpp)
target_link_libraries(mtp-tests PRIVATE mtp1)
if(MSVC)
    target_compile_options(mtp-tests PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME mtp-tests COMMAND mtp-tests)
