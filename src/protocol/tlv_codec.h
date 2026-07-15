#ifndef TLV_CODEC_H
#define TLV_CODEC_H

#include <string>
#include <vector>
#include <cstdint>

namespace protocol {

struct TLV {
    uint16_t type;
    uint32_t length;
    std::string value;

    static size_t encodedSize(uint32_t valueLen) {
        return sizeof(type) + sizeof(length) + valueLen;
    }
};

class TLVCodec {
public:
    static std::string encode(const std::vector<TLV>& tlvs);
    static std::vector<TLV> decode(const std::string& data);
    static std::string encodeSingle(uint16_t type, const std::string& value);
    static bool decodeSingle(const std::string& data, TLV& tlv);
};

class ProtoCodec {
public:
    template<typename T>
    static std::string serialize(const T& msg) {
        std::string result;
        msg.SerializeToString(&result);
        return result;
    }

    template<typename T>
    static bool deserialize(const std::string& data, T& msg) {
        return msg.ParseFromArray(data.data(), data.size());
    }

    static uint32_t computeChecksum(const std::string& data);
};

class MessageWrapper {
public:
    struct Header {
        uint32_t magic;
        uint16_t version;
        uint16_t cmd;
        uint32_t bodyLen;
        uint32_t checksum;
        uint64_t seq;
    };

    static std::string wrap(uint16_t cmd, const std::string& body, uint64_t seq = 0);
    static bool unwrap(const std::string& data, Header& header, std::string& body);
    static size_t headerSize() { return sizeof(Header); }
    static const uint32_t kMagic = 0x43484154;

private:
    static Header decodeHeader(const std::string& data);
    static std::string encodeHeader(const Header& h);
};

}

#endif
