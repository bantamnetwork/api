cmake_minimum_required(VERSION 2.8)

project(bantam-client)

if (POLICY CMP0054)
    cmake_policy(SET CMP0054 NEW)
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED on)

option(BANTAM_STATIC_RUNTIME "Use static runtime" ON)

if(MSVC)
    if(BANTAM_STATIC_RUNTIME)
        foreach(flag_var
              CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
              CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
           if(${flag_var} MATCHES "/MD")
              string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
           endif()
        endforeach(flag_var)
    endif()
    add_definitions("-D_WIN32_WINNT=0x0501")
endif()

set(Boost_USE_STATIC_LIBS ${SLIB_STATIC_RUNTIME})
set(Boost_USE_MULTITHREADED ON)
set(Boost_x64 ON)
set(Boost_USE_STATIC_RUNTIME     ${SLIB_STATIC_RUNTIME})
find_package(Boost REQUIRED thread date_time filesystem system)
add_definitions(-DBOOST_COROUTINES_NO_DEPRECATION_WARNING -DBOOST_COROUTINE_NO_DEPRECATION_WARNING)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
    "${CMAKE_SOURCE_DIR}/3rdparty"
)
link_directories(${Boost_LIBRARY_DIRS})

add_subdirectory(bantam)
add_subdirectory(examples)
