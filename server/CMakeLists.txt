cmake_minimum_required(VERSION 3.16)
project(parallel_coursework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(server
    main.cpp
)

# щоб інклуди типу "data_structure/..." і "utils/..." працювали
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR})

if (WIN32)
    target_link_libraries(server PRIVATE ws2_32)
else()
    target_link_libraries(server PRIVATE pthread)
endif()