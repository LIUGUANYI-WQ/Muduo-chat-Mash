find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)
pkg_check_modules(HIREDIS REQUIRED hiredis)

add_executable(chat_server
    main.cc
    chat_server.cc
    codec.cc
    db.cc
    redis.cc
    redis_pool.cc
    session_manager.cc
    room_manager.cc
    protocol/tlv_codec.cc
    repository/mysql_user_repo.cc
    repository/mysql_friend_repo.cc
    repository/mysql_message_repo.cc
    repository/mysql_room_repo.cc
    repository/mysql_session_repo.cc
    repository/redis_session_repo.cc
    repository/redis_pubsub.cc
    service/login_service.cc
    service/chat_service.cc
    service/friend_service.cc
    service/room_service.cc
)
target_include_directories(chat_server PRIVATE ${MYSQL_INCLUDE_DIRS} ${HIREDIS_INCLUDE_DIRS})
target_link_libraries(chat_server
    chat_proto
    muduo_net
    muduo_base
    muduo_protobuf_codec
    ${PROTOBUF_LIBRARY}
    ${MYSQL_LIBRARIES}
    ${HIREDIS_LIBRARIES}
    pthread
)
