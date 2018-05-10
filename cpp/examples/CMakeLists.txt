add_executable(example_client example_client.cpp)
target_link_libraries(example_client bantam-client  ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

