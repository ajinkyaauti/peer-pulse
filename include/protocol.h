#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <vector>

namespace p2p {

// Command types
constexpr const char* CMD_CONNECT = "CONNECT";
constexpr const char* CMD_LIST = "LIST";
constexpr const char* CMD_UPLOAD = "UPLOAD";
constexpr const char* CMD_DOWNLOAD = "DOWNLOAD";
constexpr const char* CMD_DELETE = "DELETE";
constexpr const char* CMD_PUBLIC = "PUBLIC";
constexpr const char* CMD_PRIVATE = "PRIVATE";
constexpr const char* CMD_DISCONNECT = "DISCONNECT";
constexpr const char* CMD_ROOM_CREATE = "ROOM_CREATE";
constexpr const char* CMD_ROOM_JOIN = "ROOM_JOIN";
constexpr const char* CMD_ROOM_LEAVE = "ROOM_LEAVE";
constexpr const char* CMD_ROOM_MEMBERS = "ROOM_MEMBERS";
constexpr const char* CMD_ROOM_SEND = "ROOM_SEND";
constexpr const char* CMD_ROOM_FETCH = "ROOM_FETCH";

// Response codes
constexpr const char* RESP_OK = "OK";
constexpr const char* RESP_ERROR = "ERROR";
constexpr const char* RESP_READY = "READY";

// Buffer sizes
constexpr size_t BUFFER_SIZE = 4096;
constexpr size_t MAX_MESSAGE_SIZE = 65536;

class Protocol {
public:
    static std::vector<std::string> parseCommand(const std::string& command);
    static bool extractCommand(std::string& buffer, std::string& command);
    static bool isSafePeerId(const std::string& peerId);
    static bool isSafeFilename(const std::string& filename);
    static bool isSafeRoomId(const std::string& roomId);
    static bool isSafeEncodedText(const std::string& text);
    static bool parseFileSize(const std::string& value, size_t& filesize);
    static std::string formatResponse(const std::string& status, const std::string& data);
    static std::string formatFileList(const std::vector<std::string>& files);
};

} // namespace p2p

#endif // PROTOCOL_H
