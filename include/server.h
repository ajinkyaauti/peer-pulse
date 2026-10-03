#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <functional>
#include <cstdint>

#include "net_compat.h"

namespace p2p {

struct ClientInfo {
    std::string id;
    std::string ip;
    int port;
    socket_t socket;
    std::vector<std::string> files;
    std::map<std::string, bool> fileVisibility;
};

struct RoomMessage {
    uint64_t id;
    std::string username;   // peer id of the sender
    std::string text;       // percent-encoded, opaque to the server
    long long timestamp;    // unix seconds
};

struct Room {
    std::string name;                        // percent-encoded, opaque to the server
    std::map<std::string, long long> members; // peer id -> last-seen (unix seconds)
    std::vector<RoomMessage> messages;
    uint64_t nextMessageId = 1;
};

class Server {
public:
    Server(int port);
    ~Server();
    
    bool start();
    void stop();
    void run();

    // Lets DOWNLOAD responses include a peer transfer port and an issued
    // token, obtained from the peer file server that actually holds the bytes.
    void setTransferInfo(int transferPort,
                         std::function<std::string(const std::string&)> tokenIssuer);

private:
    void handleClient(socket_t clientSocket);
    void processCommand(socket_t clientSocket, const std::string& command);
    void sendResponse(socket_t clientSocket, const std::string& response);
    
    void handleConnect(socket_t clientSocket, const std::string& peerId);
    void handleList(socket_t clientSocket);
    void handleUpload(socket_t clientSocket, const std::string& filename, size_t filesize);
    void handleDownload(socket_t clientSocket, const std::string& filename);
    void handleDelete(socket_t clientSocket, const std::string& filename);
    void handleVisibility(socket_t clientSocket, const std::string& filename, bool isPublic);
    void handleDisconnect(socket_t clientSocket);

    // Chat rooms live entirely on this server; Flask is just an HTTP<->TCP bridge.
    void handleRoomCreate(socket_t clientSocket, const std::string& nameEncoded);
    void handleRoomJoin(socket_t clientSocket, const std::string& roomId);
    void handleRoomLeave(socket_t clientSocket, const std::string& roomId);
    void handleRoomMembers(socket_t clientSocket, const std::string& roomId);
    void handleRoomSend(socket_t clientSocket, const std::string& roomId, const std::string& textEncoded);
    void handleRoomFetch(socket_t clientSocket, const std::string& roomId, const std::string& sinceIdStr);
    std::string generateRoomId(); // caller must hold roomsMutex_
    
    int port_;
    socket_t serverSocket_;
    bool running_;
    std::map<socket_t, ClientInfo> clients_;
    std::mutex clientsMutex_;
    std::map<std::string, Room> rooms_;
    std::mutex roomsMutex_;
    int transferPort_;
    std::function<std::string(const std::string&)> tokenIssuer_;

    bool initializeWinsock();
    void cleanupWinsock();
};

} // namespace p2p

#endif // SERVER_H
