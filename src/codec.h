#ifndef MUDUO_CHAT_CODEC_H
#define MUDUO_CHAT_CODEC_H

#include "muduo/net/Buffer.h"
#include "muduo/net/TcpConnection.h"
#include "muduo/base/Timestamp.h"
#include "chat.pb.h"

typedef std::shared_ptr<chat::ServerMessage> ServerMessagePtr;

class ChatCodec : muduo::noncopyable
{
public:
    typedef std::function<void (const muduo::net::TcpConnectionPtr&,
                                const ServerMessagePtr&,
                                muduo::Timestamp)> ServerMessageCallback;

    typedef std::function<void (const muduo::net::TcpConnectionPtr&,
                                const chat::Envelope&,
                                muduo::Timestamp)> EnvelopeCallback;

    explicit ChatCodec(const EnvelopeCallback& cb)
        : envelopeCallback_(cb)
    {}

    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
                   muduo::Timestamp receiveTime);

    void sendServerMessage(const muduo::net::TcpConnectionPtr& conn,
                           const chat::ServerMessage& msg);

    static chat::Envelope parseEnvelope(const muduo::StringPiece& buf);
    static muduo::string serializeEnvelope(const chat::Envelope& msg);
    static muduo::string serializeServerMessage(const chat::ServerMessage& msg);

private:
    EnvelopeCallback envelopeCallback_;

    static const size_t kHeaderLen = sizeof(int32_t);
    void handleMessage(const muduo::net::TcpConnectionPtr& conn,
                       const muduo::string& payload,
                       muduo::Timestamp receiveTime);
};

#endif
