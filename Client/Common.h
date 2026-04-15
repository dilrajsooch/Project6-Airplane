/**
 * @file Common.h
 * @brief Shared definitions for the Aircraft Telemetry Client/Server system.
 *
 * Defines the network packet structure, port constants, and utility
 * functions used by both the Client and Server applications.
 *
 * Course:  CSCN73060 - Client/Server Project
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <string>

 //Network Configuration
#define DEFAULT_SERVER_IP   "127.0.0.1"   
#define SERVER_PORT         55555         
#define BACKLOG             128            

// Packet Layout 
#define TIMESTAMP_LEN       32             

// Wire-format packet transmitted from client to server.
#pragma pack(push, 1)
struct TelemetryPacket
{
    uint32_t clientID;              
    char     timestamp[TIMESTAMP_LEN]; 
    float    fuelRemaining;         
    uint8_t  isEOF;                 
};
#pragma pack(pop)
