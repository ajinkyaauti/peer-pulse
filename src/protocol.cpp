#include "protocol.h"
#include <cctype>
#include <limits>
#include <sstream>

namespace p2p {

std::vector<std::string> Protocol::parseCommand(const std::string& command) {
    std::vector<std::string> tokens;
    std::istringstream iss(command);
    std::string token;
    
    while (iss >> token) {
        tokens.push_back(token);
    }
    
    return tokens;
}

bool Protocol::extractCommand(std::string& buffer, std::string& command) {
    const auto delimiter = buffer.find('\n');
    if (delimiter == std::string::npos) {
        return false;
    }

    command = buffer.substr(0, delimiter);
    buffer.erase(0, delimiter + 1);

    if (!command.empty() && command.back() == '\r') {
        command.pop_back();
    }

    return true;
}

bool Protocol::isSafePeerId(const std::string& peerId) {
    if (peerId.empty() || peerId.size() > 64) {
        return false;
    }

    for (const unsigned char character : peerId) {
        if (!std::isalnum(character) && character != '_' && character != '-') {
            return false;
        }
    }

    return true;
}

bool Protocol::isSafeFilename(const std::string& filename) {
    if (filename.empty() || filename == "." || filename == ".." || filename.size() > 255) {
        return false;
    }

    for (const unsigned char character : filename) {
        if (!std::isalnum(character) && character != '_' && character != '-' && character != '.') {
            return false;
        }
    }

    return true;
}

bool Protocol::isSafeRoomId(const std::string& roomId) {
    if (roomId.size() < 4 || roomId.size() > 10) {
        return false;
    }

    for (const unsigned char character : roomId) {
        if (!std::isalnum(character)) {
            return false;
        }
    }

    return true;
}

bool Protocol::isSafeEncodedText(const std::string& text) {
    // Caller (Flask) percent-encodes room names/messages before sending, so the
    // server only ever sees a single space-free token here.
    if (text.empty() || text.size() > 4000) {
        return false;
    }

    for (const unsigned char character : text) {
        if (!std::isalnum(character) && character != '%' && character != '-' &&
            character != '_' && character != '.' && character != '~') {
            return false;
        }
    }

    return true;
}

bool Protocol::parseFileSize(const std::string& value, size_t& filesize) {
    if (value.empty()) {
        return false;
    }

    size_t parsed = 0;
    for (const unsigned char character : value) {
        if (!std::isdigit(character)) {
            return false;
        }

        const size_t digit = static_cast<size_t>(character - '0');
        if (parsed > (std::numeric_limits<size_t>::max() - digit) / 10) {
            return false;
        }

        parsed = parsed * 10 + digit;
    }

    filesize = parsed;
    return true;
}

std::string Protocol::formatResponse(const std::string& status, const std::string& data) {
    std::string response = status;
    if (!data.empty()) {
        response += " " + data;
    }
    response += "\n";
    return response;
}

std::string Protocol::formatFileList(const std::vector<std::string>& files) {
    std::ostringstream oss;
    oss << files.size();
    for (const auto& file : files) {
        oss << " " << file;
    }
    return oss.str();
}

} // namespace p2p
