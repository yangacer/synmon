cmake_minimum_required (VERSION 2.8)
project (synmon)

list (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake_mod)
include (use_c++11)

find_package ( Boost REQUIRED system thread filesystem)

include_directories ( contrib ${Boost_INCLUDE_DIRS} )

add_definitions ( -DSQLITE_OMIT_LOAD_EXTENSION )
add_executable ( synmon main.cpp synmon.cpp db.cpp sqlite3.c)
target_link_libraries ( synmon ${Boost_LIBRARIES} )

