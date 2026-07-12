#include "src/codec.h"
#include "muduo/base/Logging.h"
#include "muduo/net/Endian.h"

void ChatCodec::onMessage(const muduo::net::TcpConnectionPtr& conn,
                          muduo::net::Buffer* buf,
                          muduo::Timestamp receiveTime)
{
    while (buf->readableBytes() >= kHeaderLen)
    {
        const void* data = buf->peek();
        int32_t be32 = *static_cast<const int32_t*>(data);
        const int32_t len = muduo::net::sockets::networkToHost32(be32);
        if (len > 65536 || len < 0)
        {
            LOG_ERROR << "Invalid length " << len;
            conn->shutdown();
            break;
        }
        else if (buf->readableBytes() >= len + kHeaderLen)
        {
            buf->retrieve(kHeaderLen);
            muduo::string payload(buf->peek(), len);
            handleMessage(conn, payload, receiveTime);
            buf->retrieve(len);
        }
        else
        {
            break;
        }
    }
}

void ChatCodec::handleMessage(const muduo::net::TcpConnectionPtr& conn,
                              const muduo::string& payload,
                              muduo::Timestamp receiveTime)
{
    chat::Envelope env;
    if (!env.ParseFromArray(payload.data(), payload.size()))
    {
        LOG_ERROR << "Failed to parse Envelope";
        chat::ServerMessage reply;
        reply.mutable_error()->set_code(1);
        reply.mutable_error()->set_reason("parse error");
        sendServerMessage(conn, reply);
        return;
    }
    envelopeCallback_(conn, env, receiveTime);
}

void ChatCodec::sendServerMessage(const muduo::net::TcpConnectionPtr& conn,
                                  const chat::ServerMessage& msg)
{
    muduo::string payload = serializeServerMessage(msg);
    muduo::net::Buffer buf;
    buf.append(payload.data(), payload.size());
    int32_t len = static_cast<int32_t>(payload.size());
    int32_t be32 = muduo::net::sockets::hostToNetwork32(len);
    buf.prepend(&be32, sizeof be32);
    conn->send(&buf);
}

muduo::string ChatCodec::serializeEnvelope(const chat::Envelope& msg)
{
    muduo::string result;
    if (!msg.SerializeToString(&result))
    {
        LOG_ERROR << "Failed to serialize Envelope";
    }
    return result;
}

muduo::string ChatCodec::serializeServerMessage(const chat::ServerMessage& msg)
{
    muduo::string result;
    if (!msg.SerializeToString(&result))
    {
        LOG_ERROR << "Failed to serialize ServerMessage";
    }
    return result;
}

chat::Envelope ChatCodec::parseEnvelope(const muduo::StringPiece& buf)
{
    chat::Envelope env;
    env.ParseFromArray(buf.data(), buf.size());
    return env;
}
