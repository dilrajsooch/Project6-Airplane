#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <stdexcept>

#include "Common.h"


/**
 * @brief Generate a unique non-zero client (aircraft) ID.
 *
 * Seeds the C random generator from the current time and process ID,
 * then assembles a 32-bit identifier from two rand() calls.
 *
 * @return A non-zero 32-bit unique client identifier.
 */
static uint32_t generateClientID()
{
    uint32_t seed = static_cast<uint32_t>(std::time(nullptr))
        ^ static_cast<uint32_t>(GetCurrentProcessId());
    std::srand(seed);
    uint32_t id = 0;
    while (id == 0)
        id = (static_cast<uint32_t>(std::rand()) << 16)
        | (static_cast<uint32_t>(std::rand()) & 0xFFFF);
    return id;
}

/**
 * @brief Parse one line of a telemetry data file.
 *
 * Two line formats are handled:
 *  - Header: "FUEL TOTAL QUANTITY,D_M_YYYY HH:MM:SS,fuel,"
 *  - Data:   "D_M_YYYY HH:MM:SS,fuel,"
 *
 * @param[in]  line    Raw line read from the telemetry file.
 * @param[out] outTS   Null-terminated timestamp string (fixed-size buffer).
 * @param[out] outFuel Parsed fuel quantity in gallons.
 * @return true on success, false if the line could not be parsed.
 */
static bool parseLine(const std::string& line,
    char               outTS[TIMESTAMP_LEN],
    float& outFuel)
{
    if (line.empty()) return false;

    // Strip a leading "FUEL TOTAL QUANTITY"
    std::string work = line;
    const std::string hdr = "FUEL TOTAL QUANTITY,";
    if (work.substr(0, hdr.size()) == hdr)
        work = work.substr(hdr.size());

    // Trim leading whitespace.
    auto start = work.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false;
    work = work.substr(start);

    // Split on the first comma: timestamp fuel
    auto comma = work.find(',');
    if (comma == std::string::npos) return false;

    std::string tsStr = work.substr(0, comma);
    std::string fuelStr = work.substr(comma + 1);

    // Remove any trailing comma 
    auto fuelEnd = fuelStr.find_first_of(", \t\r\n");
    if (fuelEnd != std::string::npos)
        fuelStr = fuelStr.substr(0, fuelEnd);

    if (tsStr.empty() || fuelStr.empty()) return false;

    // Copy timestamp into fixed-size buffer.
    std::strncpy(outTS, tsStr.c_str(), TIMESTAMP_LEN - 1);
    outTS[TIMESTAMP_LEN - 1] = '\0';

    // Parse fuel value.
    try
    {
        outFuel = std::stof(fuelStr);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

/**
 * @brief Reliably send all bytes in a buffer over a TCP socket.
 *
 * Loops on send() until the full length has been transmitted.
 *
 * @param s   Connected TCP socket.
 * @param buf Pointer to the data buffer.
 * @param len Number of bytes to send.
 * @return true on success, false if the connection was lost.
 */
static bool sendAll(SOCKET s, const char* buf, int len)
{
    int sent = 0;
    while (sent < len)
    {
        int n = send(s, buf + sent, len - sent, 0);
        if (n == SOCKET_ERROR || n == 0)
            return false;
        sent += n;
    }
    return true;
}

/**
 * @brief Client entry point.
 *
 * Opens the telemetry file, connects to the server, then streams
 * every parsed sample as a TelemetryPacket. A final packet with
 * @c isEOF=1 is sent so the server can finalise the flight average.
 *
 * @param argc Argument count.
 * @param argv argv[1] = server IP (optional), argv[2] = data-file path (optional).
 * @return 0 on success, non-zero on failure.
 */
int main(int argc, char* argv[])
{
    // CLI
    const char* serverIP = (argc >= 2) ? argv[1] : DEFAULT_SERVER_IP;
    const char* dataFile = (argc >= 3) ? argv[2] : "katl-kefd-B737-700.txt";

    // assign unique aircraft ID 
    uint32_t clientID = generateClientID();
    std::cout << "Aircraft ID: " << clientID << "\n"
        << "Server:      " << serverIP << ":" << SERVER_PORT << "\n"
        << "Data file:   " << dataFile << "\n\n";

    // Open telemetry file 
    std::ifstream telFile(dataFile);
    if (!telFile.is_open())
    {
        std::cerr << "ERROR: Cannot open telemetry file: " << dataFile << "\n";
        return 1;
    }

    // init connection
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << "\n";
        return 1;
    }

    // socket init, connect to server
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        std::cerr << "socket() failed: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, serverIP, &serverAddr.sin_addr) != 1)
    {
        std::cerr << "Invalid server IP: " << serverIP << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (connect(sock,
        reinterpret_cast<sockaddr*>(&serverAddr),
        sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "connect() failed: " << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to server.\n";

    // read, put in packet and transmit until EOF 
    std::string line;
uint64_t    linesSent = 0;
uint64_t    linesSkipped = 0;
bool        connectionOK = true;

    // rewind to start of file for each loop it
    telFile.clear();
    telFile.seekg(0);

    while (std::getline(telFile, line) && connectionOK)
    {
        TelemetryPacket pkt{};
        float fuel = 0.0f;

        // read while parse
        if (!parseLine(line, pkt.timestamp, fuel))
        {
            ++linesSkipped;
            continue;
        }

        // packet
        pkt.clientID = clientID;
        pkt.fuelRemaining = fuel;
        pkt.isEOF = 0;

        // transmit
        if (!sendAll(sock,
            reinterpret_cast<const char*>(&pkt),
            static_cast<int>(sizeof(pkt))))
        {
            std::cerr << "Send failed on line "
                << (linesSent + linesSkipped + 1) << "\n";
            connectionOK = false;
            break;
        }
        ++linesSent;

        
        Sleep(1);
    }

    //  Send EOF alert so the server can calculate the flight average
    if (connectionOK)
    {
        TelemetryPacket eofPkt{};
        eofPkt.clientID = clientID;
        eofPkt.isEOF = 1;

        // reuse the last valid timestamp / fuel value already in pkt context
        // the server ignores data fields when isEOF == 1.
        sendAll(sock,
            reinterpret_cast<const char*>(&eofPkt),
            static_cast<int>(sizeof(eofPkt)));
    }

    // clean/close
    std::cout << "Flight complete. Lines sent: " << linesSent
        << " | Skipped: " << linesSkipped << "\n";

    closesocket(sock);
    WSACleanup();
    return 0;
}













