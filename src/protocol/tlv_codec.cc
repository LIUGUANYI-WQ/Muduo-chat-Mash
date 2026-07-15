#include "tlv_codec.h"
#include <cstring>

namespace protocol {

std::string TLVCodec::encode(const std::vector<TLV>& tlvs) {
    std::string result;
    for (const auto& tlv : tlvs) {
        uint16_t type = htobe16(tlv.type);
        uint32_t length = htobe32(tlv.length);
        result.append(reinterpret_cast<const char*>(&type), sizeof(type));
        result.append(reinterpret_cast<const char*>(&length), sizeof(length));
        result.append(tlv.value);
    }
    return result;
}

std::vector<TLV> TLVCodec::decode(const std::string& data) {
    std::vector<TLV> tlvs;
    size_t offset = 0;
    while (offset + sizeof(uint16_t) + sizeof(uint32_t) <= data.size()) {
        uint16_t type = be16toh(*reinterpret_cast<const uint16_t*>(data.data() + offset));
        offset += sizeof(uint16_t);

        uint32_t length = be32toh(*reinterpret_cast<const uint32_t*>(data.data() + offset));
        offset += sizeof(uint32_t);

        if (offset + length > data.size())
            break;

        TLV tlv;
        tlv.type = type;
        tlv.length = length;
        tlv.value = data.substr(offset, length);
        tlvs.push_back(tlv);
        offset += length;
    }
    return tlvs;
}

std::string TLVCodec::encodeSingle(uint16_t type, const std::string& value) {
    std::string result;
    uint16_t beType = htobe16(type);
    uint32_t beLen = htobe32(static_cast<uint32_t>(value.size()));
    result.append(reinterpret_cast<const char*>(&beType), sizeof(beType));
    result.append(reinterpret_cast<const char*>(&beLen), sizeof(beLen));
    result.append(value);
    return result;
}

bool TLVCodec::decodeSingle(const std::string& data, TLV& tlv) {
    if (data.size() < sizeof(uint16_t) + sizeof(uint32_t))
        return false;

    tlv.type = be16toh(*reinterpret_cast<const uint16_t*>(data.data()));
    tlv.length = be32toh(*reinterpret_cast<const uint32_t*>(data.data() + sizeof(uint16_t)));

    if (data.size() < sizeof(uint16_t) + sizeof(uint32_t) + tlv.length)
        return false;

    tlv.value = data.substr(sizeof(uint16_t) + sizeof(uint32_t), tlv.length);
    return true;
}

uint32_t ProtoCodec::computeChecksum(const std::string& data) {
    uint32_t checksum = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        checksum += static_cast<uint8_t>(data[i]);
    }
    return checksum;
}

MessageWrapper::Header MessageWrapper::decodeHeader(const std::string& data) {
    Header h;
    std::memcpy(&h, data.data(), sizeof(Header));
    h.magic = be32toh(h.magic);
    h.version = be16toh(h.version);
    h.cmd = be16toh(h.cmd);
    h.bodyLen = be32toh(h.bodyLen);
    h.checksum = be32toh(h.checksum);
    h.seq = be64toh(h.seq);
    return h;
}

std::string MessageWrapper::encodeHeader(const Header& h) {
    Header be = h;
    be.magic = htobe32(be.magic);
    be.version = htobe16(be.version);
    be.cmd = htobe16(be.cmd);
    be.bodyLen = htobe32(be.bodyLen);
    be.checksum = htobe32(be.checksum);
    be.seq = htobe64(be.seq);
    return std::string(reinterpret_cast<const char*>(&be), sizeof(be));
}

std::string MessageWrapper::wrap(uint16_t cmd, const std::string& body, uint64_t seq) {
    Header h;
    h.magic = kMagic;
    h.version = 1;
    h.cmd = cmd;
    h.bodyLen = static_cast<uint32_t>(body.size());
    h.checksum = ProtoCodec::computeChecksum(body);
    h.seq = seq;

    std::string result = encodeHeader(h);
    result.append(body);
    return result;
}

bool MessageWrapper::unwrap(const std::string& data, Header& header, std::string& body) {
    if (data.size() < sizeof(Header))
        return false;

    header = decodeHeader(data);

    if (header.magic != kMagic)
        return false;

    if (data.size() < sizeof(Header) + header.bodyLen)
        return false;

    body = data.substr(sizeof(Header), header.bodyLen);

    uint32_t computedChecksum = ProtoCodec::computeChecksum(body);
    if (computedChecksum != header.checksum)
        return false;

    return true;
}

}
