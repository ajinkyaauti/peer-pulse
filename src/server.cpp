#include "server.h"
#include "protocol.h"
#include <iostream>
#include <thread>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <random>
#include <chrono>

namespace p2p {

namespace {
long long nowSeconds() {
    return static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

constexpr long long kRoomOnlineWindowSeconds = 30;
} // namespace

Server::Server(int port) 
    : port_(port), serverSocket_(INVALID_SOCKET), running_(false), transferPort_(0) {
}

Server::~Server() {
    stop();
}

bool Server::initializeWinsock() {
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }
#endif
    return true;
}

void Server::cleanupWinsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

bool Server::start() {
    if (!initializeWinsock()) {
        return false;
    }
    
    // Create socket
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        cleanupWinsock();
        return false;
    }
    
    // Set socket options
    int opt = 1;
#ifdef _WIN32
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    // Bind socket
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port_);
    
    if (bind(serverSocket_, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed" << std::endl;
        closesocket(serverSocket_);
        cleanupWinsock();
        return false;
    }
    
    // Listen for connections
    if (listen(serverSocket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed" << std::endl;
        closesocket(serverSocket_);
        cleanupWinsock();
        return false;
    }
    
    running_ = true;
    std::cout << "Server started on port " << port_ << std::endl;
    return true;
}

void Server::stop() {
    running_ = false;
    
    if (serverSocket_ != INVALID_SOCKET) {
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    }
    
    // Close all client connections
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& [socket, client] : clients_) {
        closesocket(socket);
    }
    clients_.clear();
    
    cleanupWinsock();
    std::cout << "Server stopped" << std::endl;
}

void Server::setTransferInfo(int transferPort,
                              std::function<std::string(const std::string&)> tokenIssuer) {
    transferPort_ = transferPort;
    tokenIssuer_ = std::move(tokenIssuer);
}


void Server::run() {
    while (running_) {
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        
        socket_t clientSocket = accept(serverSocket_, (sockaddr*)&clientAddr, 
#ifdef _WIN32
            &clientAddrSize
#else
            (socklen_t*)&clientAddrSize
#endif
        );
        
        if (clientSocket == INVALID_SOCKET) {
            if (running_) {
                std::cerr << "Accept failed" << std::endl;
            }
            continue;
        }
        
        // Get client IP
        char clientIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIp, INET_ADDRSTRLEN);
        
        std::cout << "New connection from " << clientIp << std::endl;
        
        // Handle client in a new thread
        std::thread([this, clientSocket]() {
            handleClient(clientSocket);
        }).detach();
    }
}

void Server::handleClient(socket_t clientSocket) {
    std::string pendingData;
    char buffer[BUFFER_SIZE];

    try {
        while (running_) {
            const int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
            if (bytesReceived <= 0) {
                break; // Client disconnected
            }

            pendingData.append(buffer, static_cast<size_t>(bytesReceived));
            if (pendingData.size() > MAX_MESSAGE_SIZE) {
                sendResponse(clientSocket, Protocol::formatResponse(
                    RESP_ERROR, "Message too large"));
                break;
            }

            std::string command;
            while (Protocol::extractCommand(pendingData, command)) {
                processCommand(clientSocket, command);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error handling client: " << e.what() << std::endl;
    }
    
    handleDisconnect(clientSocket);
    closesocket(clientSocket);
}

void Server::sendResponse(socket_t clientSocket, const std::string& response) {
    size_t bytesSent = 0;
    while (bytesSent < response.size()) {
        const size_t remaining = response.size() - bytesSent;
        const int chunkSize = static_cast<int>(std::min<size_t>(remaining, 64 * 1024));
        const int result = send(clientSocket, response.data() + bytesSent, chunkSize, 0);
        if (result <= 0) {
            std::cerr << "Failed to send response" << std::endl;
            return;
        }

        bytesSent += static_cast<size_t>(result);
    }
}

void Server::processCommand(socket_t clientSocket, const std::string& command) {
    auto tokens = Protocol::parseCommand(command);
    
    if (tokens.empty()) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Empty command"));
        return;
    }
    
    const std::string& cmd = tokens[0];
    
    if (cmd == CMD_CONNECT && tokens.size() == 2) {
        if (!Protocol::isSafePeerId(tokens[1])) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid peer ID"));
            return;
        }
        handleConnect(clientSocket, tokens[1]);
    } else if (cmd == CMD_LIST && tokens.size() == 1) {
        handleList(clientSocket);
    } else if (cmd == CMD_UPLOAD && tokens.size() == 3) {
        size_t filesize = 0;
        if (!Protocol::isSafeFilename(tokens[1]) ||
            !Protocol::parseFileSize(tokens[2], filesize)) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR,
                "Invalid upload arguments"));
            return;
        }
        handleUpload(clientSocket, tokens[1], filesize);
    } else if (cmd == CMD_DOWNLOAD && tokens.size() == 2) {
        if (!Protocol::isSafeFilename(tokens[1])) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid filename"));
            return;
        }
        handleDownload(clientSocket, tokens[1]);
    } else if ((cmd == CMD_DELETE || cmd == CMD_PUBLIC || cmd == CMD_PRIVATE) &&
               tokens.size() == 2) {
        if (!Protocol::isSafeFilename(tokens[1])) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid filename"));
            return;
        }
        if (cmd == CMD_DELETE) {
            handleDelete(clientSocket, tokens[1]);
        } else {
            handleVisibility(clientSocket, tokens[1], cmd == CMD_PUBLIC);
        }
    } else if (cmd == CMD_DISCONNECT && tokens.size() == 1) {
        handleDisconnect(clientSocket);
    } else if (cmd == CMD_ROOM_CREATE && tokens.size() == 2) {
        if (!Protocol::isSafeEncodedText(tokens[1])) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid room name"));
            return;
        }
        handleRoomCreate(clientSocket, tokens[1]);
    } else if (cmd == CMD_ROOM_JOIN && tokens.size() == 2) {
        if (!Protocol::isSafeRoomId(tokens[1])) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid room ID"));
            return;
        }
        handleRoomJoin(clientSocket, tokens[1]);
    } else if (cmd == CMD_ROOM_LEAVE && tokens.size() == 2) {
        if (!Protocol::isSafeRoomId(tokens[1])) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid room ID"));
            return;
        }
        handleRoomLeave(clientSocket, tokens[1]);
    } else if (cmd == CMD_ROOM_MEMBERS && tokens.size() == 2) {
        if (!Protocol::isSafeRoomId(tokens[1])) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid room ID"));
            return;
        }
        handleRoomMembers(clientSocket, tokens[1]);
    } else if (cmd == CMD_ROOM_SEND && tokens.size() == 3) {
        if (!Protocol::isSafeRoomId(tokens[1]) || !Protocol::isSafeEncodedText(tokens[2])) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid arguments"));
            return;
        }
        handleRoomSend(clientSocket, tokens[1], tokens[2]);
    } else if (cmd == CMD_ROOM_FETCH && tokens.size() == 3) {
        if (!Protocol::isSafeRoomId(tokens[1])) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid room ID"));
            return;
        }
        handleRoomFetch(clientSocket, tokens[1], tokens[2]);
    } else if (cmd == CMD_LIST ||
               (cmd == CMD_CONNECT && tokens.size() != 2) ||
               (cmd == CMD_UPLOAD && tokens.size() != 3) ||
               (cmd == CMD_DOWNLOAD && tokens.size() != 2) ||
               ((cmd == CMD_DELETE || cmd == CMD_PUBLIC || cmd == CMD_PRIVATE) &&
                tokens.size() != 2) || (cmd == CMD_DISCONNECT && tokens.size() != 1)) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid command arguments"));
    } else {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Unknown command"));
    }
}

void Server::handleConnect(socket_t clientSocket, const std::string& peerId) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    
    ClientInfo info;
    info.id = peerId;
    info.socket = clientSocket;
    clients_[clientSocket] = info;
    
    std::cout << "Client registered: " << peerId << std::endl;
    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK, "Connected"));
}

std::string Server::generateRoomId() {
    static const std::string alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no ambiguous 0/O/1/I
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, alphabet.size() - 1);

    for (int attempt = 0; attempt < 20; ++attempt) {
        std::string candidate;
        for (int i = 0; i < 6; ++i) {
            candidate += alphabet[dist(rng)];
        }
        if (rooms_.find(candidate) == rooms_.end()) {
            return candidate;
        }
    }
    return "ROOM01";
}

void Server::handleRoomCreate(socket_t clientSocket, const std::string& nameEncoded) {
    std::string username;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto clientIt = clients_.find(clientSocket);
        if (clientIt == clients_.end()) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Not connected"));
            return;
        }
        username = clientIt->second.id;
    }

    std::lock_guard<std::mutex> roomsLock(roomsMutex_);
    const std::string roomId = generateRoomId();
    Room room;
    room.name = nameEncoded;
    room.members[username] = nowSeconds();
    rooms_[roomId] = std::move(room);

    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK, roomId + " " + nameEncoded));
}

void Server::handleRoomJoin(socket_t clientSocket, const std::string& roomId) {
    std::string username;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto clientIt = clients_.find(clientSocket);
        if (clientIt == clients_.end()) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Not connected"));
            return;
        }
        username = clientIt->second.id;
    }

    std::lock_guard<std::mutex> roomsLock(roomsMutex_);
    auto roomIt = rooms_.find(roomId);
    if (roomIt == rooms_.end()) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Room not found"));
        return;
    }
    roomIt->second.members[username] = nowSeconds();
    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK, roomId + " " + roomIt->second.name));
}

void Server::handleRoomLeave(socket_t clientSocket, const std::string& roomId) {
    std::string username;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto clientIt = clients_.find(clientSocket);
        if (clientIt == clients_.end()) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Not connected"));
            return;
        }
        username = clientIt->second.id;
    }

    std::lock_guard<std::mutex> roomsLock(roomsMutex_);
    auto roomIt = rooms_.find(roomId);
    if (roomIt != rooms_.end()) {
        roomIt->second.members.erase(username);
    }
    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK, ""));
}

void Server::handleRoomMembers(socket_t clientSocket, const std::string& roomId) {
    std::string username;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto clientIt = clients_.find(clientSocket);
        if (clientIt == clients_.end()) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Not connected"));
            return;
        }
        username = clientIt->second.id;
    }

    std::lock_guard<std::mutex> roomsLock(roomsMutex_);
    auto roomIt = rooms_.find(roomId);
    if (roomIt == rooms_.end() || roomIt->second.members.find(username) == roomIt->second.members.end()) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Room not found"));
        return;
    }

    const long long now = nowSeconds();
    std::ostringstream oss;
    oss << roomIt->second.members.size();
    for (const auto& [member, lastSeen] : roomIt->second.members) {
        const bool online = (now - lastSeen) <= kRoomOnlineWindowSeconds;
        oss << " " << member << ":" << (online ? 1 : 0);
    }
    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK, oss.str()));
}

void Server::handleRoomSend(socket_t clientSocket, const std::string& roomId, const std::string& textEncoded) {
    std::string username;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto clientIt = clients_.find(clientSocket);
        if (clientIt == clients_.end()) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Not connected"));
            return;
        }
        username = clientIt->second.id;
    }

    std::lock_guard<std::mutex> roomsLock(roomsMutex_);
    auto roomIt = rooms_.find(roomId);
    if (roomIt == rooms_.end() || roomIt->second.members.find(username) == roomIt->second.members.end()) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Room not found"));
        return;
    }

    const long long now = nowSeconds();
    roomIt->second.members[username] = now;

    RoomMessage msg;
    msg.id = roomIt->second.nextMessageId++;
    msg.username = username;
    msg.text = textEncoded;
    msg.timestamp = now;
    roomIt->second.messages.push_back(msg);
    if (roomIt->second.messages.size() > 500) {
        roomIt->second.messages.erase(roomIt->second.messages.begin());
    }

    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK, std::to_string(msg.id)));
}

void Server::handleRoomFetch(socket_t clientSocket, const std::string& roomId, const std::string& sinceIdStr) {
    std::string username;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto clientIt = clients_.find(clientSocket);
        if (clientIt == clients_.end()) {
            sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Not connected"));
            return;
        }
        username = clientIt->second.id;
    }

    uint64_t sinceId = 0;
    try {
        sinceId = std::stoull(sinceIdStr);
    } catch (const std::exception&) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Invalid since_id"));
        return;
    }

    std::lock_guard<std::mutex> roomsLock(roomsMutex_);
    auto roomIt = rooms_.find(roomId);
    if (roomIt == rooms_.end() || roomIt->second.members.find(username) == roomIt->second.members.end()) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Room not found"));
        return;
    }
    roomIt->second.members[username] = nowSeconds();

    std::ostringstream oss;
    std::ostringstream body;
    size_t matched = 0;
    for (const auto& msg : roomIt->second.messages) {
        if (msg.id > sinceId) {
            body << " " << msg.id << ":" << msg.username << ":" << msg.timestamp << ":" << msg.text;
            ++matched;
        }
    }
    oss << matched << body.str();
    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK, oss.str()));
}

void Server::handleList(socket_t clientSocket) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    
    std::vector<std::string> allFiles;
    for (const auto& [socket, client] : clients_) {
        for (const auto& file : client.files) {
            const auto visibility = client.fileVisibility.find(file);
            if (visibility != client.fileVisibility.end() && visibility->second) {
                allFiles.push_back(client.id + ":" + file);
            }
        }
    }
    
    std::string fileList = Protocol::formatFileList(allFiles);
    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK, fileList));
}

void Server::handleUpload(socket_t clientSocket, const std::string& filename, size_t filesize) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    
    auto it = clients_.find(clientSocket);
    if (it != clients_.end()) {
        it->second.files.push_back(filename);
        it->second.fileVisibility[filename] = true;
        std::cout << "Client " << it->second.id << " uploaded: " << filename 
                  << " (" << filesize << " bytes)" << std::endl;
        sendResponse(clientSocket, Protocol::formatResponse(RESP_OK, "Upload registered"));
    } else {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Not connected"));
    }
}

void Server::handleDownload(socket_t clientSocket, const std::string& filename) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    
    // Find the file owner
    for (const auto& [socket, client] : clients_) {
        for (const auto& file : client.files) {
            const auto visibility = client.fileVisibility.find(file);
            if (file == filename && visibility != client.fileVisibility.end() && visibility->second) {
                if (tokenIssuer_) {
                    const std::string token = tokenIssuer_(filename);
                    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK,
                        client.id + " " + std::to_string(transferPort_) + " " + token));
                } else {
                    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK,
                        "File found: " + client.id));
                }
                return;
            }
        }
    }
    
    sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "File not found"));
}

void Server::handleDelete(socket_t clientSocket, const std::string& filename) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto client = clients_.find(clientSocket);
    if (client == clients_.end()) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Not connected"));
        return;
    }

    auto file = std::find(client->second.files.begin(), client->second.files.end(), filename);
    if (file == client->second.files.end()) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "File not found"));
        return;
    }

    client->second.files.erase(file);
    client->second.fileVisibility.erase(filename);
    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK, "File deleted"));
}

void Server::handleVisibility(socket_t clientSocket, const std::string& filename, bool isPublic) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto client = clients_.find(clientSocket);
    if (client == clients_.end()) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "Not connected"));
        return;
    }

    if (client->second.fileVisibility.find(filename) == client->second.fileVisibility.end()) {
        sendResponse(clientSocket, Protocol::formatResponse(RESP_ERROR, "File not found"));
        return;
    }

    client->second.fileVisibility[filename] = isPublic;
    sendResponse(clientSocket, Protocol::formatResponse(RESP_OK,
        isPublic ? "File is public" : "File is private"));
}

void Server::handleDisconnect(socket_t clientSocket) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    
    auto it = clients_.find(clientSocket);
    if (it != clients_.end()) {
        std::cout << "Client disconnected: " << it->second.id << std::endl;
        clients_.erase(it);
    }
}

} // namespace p2p
