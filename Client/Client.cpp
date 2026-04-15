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