cmake_minimum_required (VERSION 2.8)
project (synmon)

list (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake_mod)
include (use_c++11)
include (disable_useless_vc_warning)

if(MSVC)
  message("Force static link to msvc runtime")
  include (force_static_msvc_runtime)
  set (Boost_USE_STATIC_LIBS ON)
  set (Boost_USE_STATIC_RUNTIME ON)
  set (Boost_USE_MULTITHREADED ON)
  add_definitions ( -D_WIN32_WINNT=0x0501 -DMP )
endif()

set (Boost_ADDITIONAL_VERSIONS "1.53" "1.53.0" )
find_package ( Boost REQUIRED COMPONENTS
               locale
               system
               filesystem 
               thread)

#find_package ( OpenSSL REQUIRED )

include_directories ( contrib 
                      contrib/json 
                      contrib/agent
                      ${Boost_INCLUDE_DIRS} )

link_directories(${Boost_LIBRARY_DIRS})

add_subdirectory ( contrib/agent )
add_subdirectory ( contrib/json )

add_definitions ( -DSQLITE_OMIT_LOAD_EXTENSION )
add_library ( libsynmon STATIC synmon.cpp db.cpp sqlite3.c dl_ctx.cpp error.cpp)
target_link_libraries ( libsynmon ${Boost_LIBRARIES} json agent )

add_executable ( synmon main.cpp )
target_link_libraries ( synmon libsynmon )

add_executable ( auth auth.cpp )
target_link_libraries ( auth libsynmon )

add_executable ( driver driver.cpp )
target_link_libraries ( driver libsynmon )

#add_executable ( testcp testcp.cpp )
