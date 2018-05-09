SET(CLIENT_FILES
    client.cpp
    client.h
    order_book.h
    )
add_library(bantam-client STATIC ${CLIENT_FILES})


