cmake_minimum_required(VERSION 3.16)

project(PrithivMart)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)

add_executable(
    PrithivMart
    main.cc
)

target_include_directories(
    PrithivMart PRIVATE
    "C:/Program Files/PostgreSQL/18/include"
)

target_link_libraries(
    PrithivMart PRIVATE
    Drogon::Drogon
    CURL::libcurl
    "C:/Program Files/PostgreSQL/18/lib/libpq.lib"
)
