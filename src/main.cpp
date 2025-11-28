#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <ostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define DISCOVERY_MSG "WHO_IS_SERVER"
#define RESPONSE_MSG "I_AM_SERVER"

#define DISCOVERY_PORT 56006
#define SERVICE_PORT 60065
#define SAVE_DIRECTORY "./received_files/"

std::string filePath;
int sender_num;

int receiver_num;

int peer_num;

bool has_file;
bool is_sender;

// TODO put verifications for arguments
int handle_args(int argc, char *argv[]) {
    for (int i = 0; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-s") {
            is_sender = true;
            sender_num = std::stoi(argv[i + 1]);
        } else if (arg == "-r") {
            is_sender = false;
            receiver_num = std::stoi(argv[i + 1]);
        } else if (arg == "-f") {
            has_file = true;
            filePath = argv[i + 1];
        } else if (arg == "-p") {
            peer_num = std::stoi(argv[i + 1]);
        }
    }

    if ((has_file && !is_sender) || (!has_file && is_sender)) {
        return -1;
    }

    return 0;
}

// just discover the IP of a sender
std::string discover_sender(int maxAttempts = 100) {
    int attempt = 0;

    while (maxAttempts == 0 || attempt < maxAttempts) {
        attempt++;

        if (maxAttempts == 0) {
            std::cout << "\n[Attempt " << attempt << "] Searching for server on network..."
                      << std::endl;
        } else {
            std::cout << "\n[Attempt " << attempt << "/" << maxAttempts
                      << "] Searching for server on network..." << std::endl;
        }

        int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);

        int broadcast = 1;
        setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

        struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sockaddr_in broadcastAddr;
        broadcastAddr.sin_family = AF_INET;
        broadcastAddr.sin_port = htons(DISCOVERY_PORT);
        broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

        sendto(udpSocket, DISCOVERY_MSG, strlen(DISCOVERY_MSG), 0,
               (struct sockaddr *)&broadcastAddr, sizeof(broadcastAddr));

        char buffer[1024] = {0};
        sockaddr_in serverAddr;
        socklen_t serverLen = sizeof(serverAddr);

        int bytesReceived = recvfrom(udpSocket, buffer, sizeof(buffer), 0,
                                     (struct sockaddr *)&serverAddr, &serverLen);

        close(udpSocket);

        if (bytesReceived > 0 && strcmp(buffer, RESPONSE_MSG) == 0) {
            std::string serverIP = inet_ntoa(serverAddr.sin_addr);
            std::cout << "✓ Server found at: " << serverIP << std::endl;
            return serverIP;
        }
        std::cout << "✗ No server found. ";
        if (maxAttempts == 0) {
            std::cout << "Retrying in 2 seconds... (Press Ctrl+C to stop)" << std::endl;
        } else if (attempt < maxAttempts) {
            std::cout << "Retrying in 2 seconds..." << std::endl;
        } else {
            std::cout << "Max attempts reached." << std::endl;
        }

        if (maxAttempts == 0 || attempt < maxAttempts) {
            sleep(2);
        }
    }

    return "";
}
// just discover the IP of a receiver
void discover_receiver() {
    int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);

    int broadcast = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    int reuse = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in discoveryAddr;
    discoveryAddr.sin_family = AF_INET;
    discoveryAddr.sin_port = htons(DISCOVERY_PORT);
    discoveryAddr.sin_addr.s_addr = INADDR_ANY;

    int i = bind(udpSocket, (struct sockaddr *)&discoveryAddr, sizeof(discoveryAddr));

    std::cout << "Discovery service listening on port " << DISCOVERY_PORT << std::endl;

    while (true) {
        char buffer[1024] = {0};
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);

        recvfrom(udpSocket, buffer, sizeof(buffer), 0, (struct sockaddr *)&clientAddr, &clientLen);

        if (strcmp(buffer, DISCOVERY_MSG) == 0) {
            std::cout << "Discovery request received from " << inet_ntoa(clientAddr.sin_addr)
                      << std::endl;

            sendto(udpSocket, RESPONSE_MSG, strlen(RESPONSE_MSG), 0, (struct sockaddr *)&clientAddr,
                   clientLen);
        }
    }

    close(udpSocket);
}

// verify if sender is the one
void pair_sender() {}
// verify if receiver is right
void pair_receiver() {}

// sender sending file
bool send_file(int socketFd) {
    std::ifstream dataFile(filePath, std::ios::binary);
    if (!dataFile.is_open()) {
        std::cerr << "Error: Could not open file: " << filePath << std::endl;
        return false;
    }

    // Obter tamanho do arquivo
    dataFile.seekg(0, std::ios::end);
    uint64_t fileSize = dataFile.tellg();
    dataFile.seekg(0, std::ios::beg);

    std::cout << "File size: " << fileSize << " bytes" << std::endl;

    // Extrair nome do arquivo
    std::string filename = filePath;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }

    // Enviar tamanho do nome do arquivo
    uint32_t filenameLen = filename.length();
    send(socketFd, &filenameLen, sizeof(filenameLen), 0);

    // Enviar nome do arquivo
    send(socketFd, filename.c_str(), filenameLen, 0);

    // Enviar tamanho do arquivo
    send(socketFd, &fileSize, sizeof(fileSize), 0);

    // Ler e enviar arquivo em chunks
    const size_t CHUNK_SIZE = 4096;
    std::vector<char> buffer(CHUNK_SIZE);
    uint64_t totalSent = 0;

    while (totalSent < fileSize) {
        size_t toRead = std::min(CHUNK_SIZE, (size_t)(fileSize - totalSent));
        dataFile.read(buffer.data(), toRead);

        ssize_t sent = send(socketFd, buffer.data(), toRead, 0);
        if (sent < 0) {
            std::cerr << "Error sending data" << std::endl;
            return false;
        }

        totalSent += sent;

        // Mostrar progresso
        int progress = (totalSent * 100) / fileSize;
        std::cout << "\rSending: " << progress << "% (" << totalSent << "/" << fileSize << " bytes)"
                  << std::flush;
    }

    std::cout << std::endl << "File sent successfully!" << std::endl;
    dataFile.close();
    return true;
}
// receiver receiving file
bool receive_file(int socketFd) {
    // Receber tamanho do nome do arquivo
    uint32_t filenameLen;
    recv(socketFd, &filenameLen, sizeof(filenameLen), MSG_WAITALL);

    if (filenameLen == 0 || filenameLen > 255) {
        std::cerr << "Invalid filename length" << std::endl;
        return false;
    }

    // Receber nome do arquivo
    std::vector<char> filenameBuffer(filenameLen + 1, 0);
    recv(socketFd, filenameBuffer.data(), filenameLen, MSG_WAITALL);
    std::string filename(filenameBuffer.data());

    std::cout << "Receiving file: " << filename << std::endl;

    // Receber tamanho do arquivo
    uint64_t fileSize;
    recv(socketFd, &fileSize, sizeof(fileSize), MSG_WAITALL);

    std::cout << "File size: " << fileSize << " bytes" << std::endl;

    // Criar diretório se não existir
    system(("mkdir -p " + std::string(SAVE_DIRECTORY)).c_str());

    // Abrir arquivo para escrita
    std::string filepath = std::string(SAVE_DIRECTORY) + filename;
    std::ofstream outputFile(filepath, std::ios::binary);

    if (!outputFile.is_open()) {
        std::cerr << "Error: Could not create file: " << filepath << std::endl;
        return false;
    }

    // Receber dados em chunks
    const size_t CHUNK_SIZE = 4096;
    std::vector<char> buffer(CHUNK_SIZE);
    uint64_t totalReceived = 0;

    while (totalReceived < fileSize) {
        size_t toReceive = std::min(CHUNK_SIZE, (size_t)(fileSize - totalReceived));
        ssize_t received = recv(socketFd, buffer.data(), toReceive, 0);

        if (received <= 0) {
            std::cerr << "Error receiving data" << std::endl;
            outputFile.close();
            return false;
        }

        outputFile.write(buffer.data(), received);
        totalReceived += received;

        // Mostrar progresso
        int progress = (totalReceived * 100) / fileSize;
        std::cout << "\rReceiving: " << progress << "% (" << totalReceived << "/" << fileSize
                  << " bytes)" << std::flush;
    }

    std::cout << std::endl << "File received and saved to: " << filepath << std::endl;
    outputFile.close();
    return true;
}

// create sender TCP socket()
int tcp_socket_sender() {
    int senderSocket = socket(AF_INET, SOCK_STREAM, 0);

    int reuse = 1;
    setsockopt(senderSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in senderAddress;
    senderAddress.sin_family = AF_INET;
    senderAddress.sin_port = htons(SERVICE_PORT);
    senderAddress.sin_addr.s_addr = INADDR_ANY;

    int i = bind(senderSocket, (struct sockaddr *)&senderAddress, sizeof(senderAddress));
    std::cout << "TCP bind result: " << i << std::endl;

    listen(senderSocket, 5);

    std::cout << "Server waiting for connections on port " << SERVICE_PORT << std::endl;
    return senderSocket;
}

int tcp_socket_receiver(std::string senderIP) {
    // Cria socket TCP para conexão
    int receiverSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in senderAddress;
    senderAddress.sin_family = AF_INET;
    senderAddress.sin_port = htons(SERVICE_PORT);
    inet_pton(AF_INET, senderIP.c_str(), &senderAddress.sin_addr);

    // Conecta ao servidor descoberto
    std::cout << "Connecting to server at " << senderIP << ":" << SERVICE_PORT << std::endl;
    int result = connect(receiverSocket, (struct sockaddr *)&senderAddress, sizeof(senderAddress));

    if (result < 0) {
        perror("Connection failed");
        return 1;
    }

    std::cout << "Connected successfully!" << std::endl;
    return receiverSocket;
}

void handle_fle_sending(int tcpSocket) {

    sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);

    int clientSocket = accept(tcpSocket, (struct sockaddr *)&clientAddr, &clientLen);
    std::cout << "\n=== Receiver connected from " << inet_ntoa(clientAddr.sin_addr)
              << " ===" << std::endl;

    // Enviar ficheiro
    bool success = send_file(clientSocket);

    // Receber confirmação
    char ackBuffer[1024] = {0};
    recv(clientSocket, ackBuffer, sizeof(ackBuffer), 0);
    std::cout << "Receiver response: " << ackBuffer << std::endl;

    close(clientSocket);
    std::cout << "=== Connection closed ===" << std::endl;

}

// normall operation of sender
void handle_sender() {
    std::cout << "Starting sender mode - listening for discovery requests..." << std::endl;
    // Inicia thread de discovery em background
    std::thread discoveryThread(discover_receiver);
    discoveryThread.detach();

    int tcpSocket = tcp_socket_sender();

    std::cout << "Waiting for receiver to connect..." << std::endl;

    handle_fle_sending(tcpSocket);

    close(tcpSocket);
}
// normall operation of receiver
void handle_receiver() {
    std::string sender_ip;
    sender_ip = discover_sender();
    if (sender_ip.empty()) {
        std::cerr << "\n✗ Unable to find server. Exiting." << std::endl;
        exit(1);
    }

    int tcpSocket = tcp_socket_receiver(sender_ip);

    // Receber ficheiro
    bool success = receive_file(tcpSocket);

    // Enviar confirmação
    const char *response = success ? "File received successfully!" : "Error receiving file";
    send(tcpSocket, response, strlen(response), 0);

    close(tcpSocket);
    std::cout << "Connection closed." << std::endl;
}

int main(int argc, char *argv[]) {

    int args_handling = 0;

    if (argc > 1) {
        args_handling = handle_args(argc, argv);

        if (args_handling == -1) {
            std::cerr << "ERROR DEALING WITH ARGUMENTS" << std::endl;
        }
    } else {
        std::cerr << "ERROR NOT ENOUGH ARGUMENTS" << std::endl;
        exit(-1);
    }

    if (is_sender) {
        handle_sender();
    } else {
        handle_receiver();
    }

    return 0;
}