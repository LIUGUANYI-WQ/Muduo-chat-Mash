find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)

add_executable(chat_server
    main.cc
    chat_server.cc
    codec.cc
    db.cc
)
target_include_directories(chat_server PRIVATE ${MYSQL_INCLUDE_DIRS})
target_link_libraries(chat_server
    chat_proto
    muduo_net
    muduo_base
    muduo_protobuf_codec
    ${PROTOBUF_LIBRARY}
    ${MYSQL_LIBRARIES}
    pthread
)
