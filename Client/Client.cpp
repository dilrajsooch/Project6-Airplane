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


// Unique ID Generation 
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

// ─── Telemetry File Parser ─────────────────────────────────────────────────────

/**
 * Parse one line of a telemetry data file.
 *
 * Two line formats are handled:
 *   Header:    "FUEL TOTAL QUANTITY,D_M_YYYY HH:MM:SS,fuel,"
 *   Data:      " D_M_YYYY HH:MM:SS,fuel,"
 *
 * @param line      Raw line string.
 * @param outTS     Output: null-terminated timestamp string.
 * @param outFuel   Output: fuel quantity (gallons).
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