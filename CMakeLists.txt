cmake_minimum_required (VERSION 2.8)
project (synmon)

list (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake_mod)
include (use_c++11)

if(MSVC)
  include (force_static_msvc_runtime)
  set (Boost_USE_STATIC_LIBS ON)
  set (Boost_USE_STATIC_RUNTIME ON)
  set (Boost_USE_MULTITHREADED ON)
  add_definitions ( -D_WIN32_WINNT=0x0501 -DMP )
endif()

set (Boost_ADDITIONAL_VERSIONS "1.53" "1.53.0" )
find_package ( Boost REQUIRED COMPONENTS
               system
               filesystem 
               thread)

include_directories ( contrib ${Boost_INCLUDE_DIRS} )
link_directories(${Boost_LIBRARY_DIRS})

add_definitions ( -DSQLITE_OMIT_LOAD_EXTENSION )
add_executable ( synmon main.cpp synmon.cpp db.cpp sqlite3.c )
target_link_libraries ( synmon ${Boost_LIBRARIES} )


