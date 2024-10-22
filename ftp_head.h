#include <iostream>
#include <cstdint>
#include <arpa/inet.h>
#include <string>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sstream>
#include <fstream>
#include <filesystem>

#define DEBUG
#define byte char
#define MAGIC_NUMBER_LENGTH 6
#define type char
#define status bool
#define PROTOCOL {'\xc1', '\xa1', '\x10', 'f', 't', 'p'}
#define MAX_BUFFER_SIZE 2048
#define MAX_FILE_SIZE 1024 * 1024

uint32_t to_big_endian(uint32_t value)
{
    return ((value & 0xFF000000) >> 24) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x000000FF) << 24);
}

uint32_t to_little_endian(uint32_t value)
{
    return ((value & 0xFF000000) >> 24) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x000000FF) << 24);
}

struct data_packet
{
    byte m_protocol[MAGIC_NUMBER_LENGTH]; /* protocol magic number (6 bytes) */
    type m_type;                          /* type (1 byte) */
    status m_status;                      /* status (1 byte) */
    uint32_t m_length;                    /* length (4 bytes) in Big endian*/
} __attribute__((packed));

// We only need to construct the myFTP Header and myFTP Data
// | 802.3 Header | IP Header | TCP Header | myFTP Header | myFTP Data |

// ********** myFTP Header ********** //
// 1. OPEN
const struct data_packet OPEN_CONN_REQUEST = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA1),
    .m_status = 0, // unused
    .m_length = to_big_endian(12)};

const struct data_packet OPEN_CONNECTION_REPLY = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA2),
    .m_status = 1,
    .m_length = to_big_endian(12)};

// 2. LIST
const struct data_packet LIST_REQUEST = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA3),
    .m_status = 0, // unused
    .m_length = to_big_endian(12)};

// Unused in ftp_client.cpp
const struct data_packet LIST_REPLY = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA4),
    .m_status = 0,                // unused
    .m_length = to_big_endian(12) // 12 + strlen(payload) + 1
    // payload = file names, as one null-terminated string
};

// 3. CD
const struct data_packet CD_REQUEST = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA5),
    .m_status = 0,                // unused
    .m_length = to_big_endian(12) // 12 + strlen(payload) + 1
    // payload = one directory name, as one null-terminated string
};

const struct data_packet CD_REPLY_SUCCESS = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA6),
    .m_status = 1,
    .m_length = to_big_endian(12)};

const struct data_packet CD_REPLY_FAIL = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA6),
    .m_status = 0,
    .m_length = to_big_endian(12)};

// 4. GET FILE
const struct data_packet GET_REQUEST = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA7),
    .m_status = 0,                // unused
    .m_length = to_big_endian(12) // 12 + strlen(payload) + 1
    // payload = one file name, as one null-terminated string
};

const struct data_packet GET_REPLY_SUCCESS = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA8),
    .m_status = 1,
    .m_length = to_big_endian(12)};

const struct data_packet GET_REPLY_FAIL = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA8),
    .m_status = 0,
    .m_length = to_big_endian(12)};

// 5. PUT FILE
const struct data_packet PUT_REQUEST = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xA9),
    .m_status = 0,                // unused
    .m_length = to_big_endian(12) // 12 + strlen(payload) + 1
    // payload = one file name, as one null-terminated string
};

const struct data_packet PUT_REPLY = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xAA),
    .m_status = 0, // unused
    .m_length = to_big_endian(12)};

// 6. SHA256
const struct data_packet SHA_REQUEST = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xAB),
    .m_status = 0,                // unused
    .m_length = to_big_endian(12) // 12 + strlen(payload) + 1
    // payload = one file name, as one null-terminated string
};

const struct data_packet SHA_REPLY_SUCCESS = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xAC),
    .m_status = 1,
    .m_length = to_big_endian(12)};

const struct data_packet SHA_REPLY_FAIL = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xAC),
    .m_status = 0,
    .m_length = to_big_endian(12)};

// 7. QUIT
const struct data_packet QUIT_REQUEST = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xAD),
    .m_status = 0, // unused
    .m_length = to_big_endian(12)};

const struct data_packet QUIT_REPLY = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xAE),
    .m_status = 0, // unused
    .m_length = to_big_endian(12)};

// 8. FILE DATA
// File data
const struct data_packet FILE_DATA = {
    .m_protocol = PROTOCOL,
    .m_type = char(0xFF),
    .m_status = 0,                // unused
    .m_length = to_big_endian(12) // 12 + filesize
    // payload = file data
};
// ********** myFTP Header ********** //

// ********** Helper Functions ********** //
ssize_t safe_send(int &sock, const char *buffer, size_t len)
{
    size_t ret = 0;
    while (ret < len)
    {
        ssize_t b = send(sock, buffer + ret, len - ret, 0);
        if (b == 0)
        {
            std::cout << "Socket closed" << std::endl;
            return -1;
        }
        if (b < 0)
        {
            std::cerr << "Error sending data" << std::endl;
            return -1;
        }
        ret += b; // Send b bytes of data successfully
    }
    return ret; // Return the number of bytes sent
}

ssize_t safe_recv(int &sock, char *buffer, size_t len, char terminator = '\0')
{
    size_t ret = 0;
    while (ret < len)
    {
        ssize_t b = recv(sock, buffer + ret, len - ret, 0);
        if (b == 0)
        {
            std::cerr << "Socket closed" << std::endl;
            return -1;
        }
        if (b < 0)
        {
            std::cerr << "Error receiving data" << std::endl;
            return -1;
        }
        // Check if the terminator is in the received data
        for (ssize_t i = 0; i < b; ++i)
        {
            if (buffer[ret + i] == terminator)
            {
                return ret + i + 1; // Return the number of bytes received so far including the terminator
            }
        }
        ret += b; // Receive b bytes of data successfully
    }
    return ret; // Return the number of bytes received
}

// ********** Helper Functions ********** //