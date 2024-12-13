#include "rtp.h"
#include "util.h"
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <map>

// Macros
#define BUFFER_SIZE 2048
#define RESEND_SYN_MAX 50
#define HEADER_SIZE sizeof(rtp_header_t)

void send_ack(int sock, uint32_t seq_num, struct sockaddr_in listen_addr)
{
    // Send the ACK
    rtp_header_t sent_ack_header;
    sent_ack_header.seq_num = seq_num;
    sent_ack_header.length = 0;
    sent_ack_header.checksum = 0;
    sent_ack_header.flags = RTP_ACK;

    // Compute checksum
    sent_ack_header.checksum = compute_checksum(&sent_ack_header, sizeof(rtp_header_t));

    sendto(sock, &sent_ack_header, sizeof(sent_ack_header), 0, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    LOG_DEBUG("Sent RTP_ACK with seq_num: %d\n", sent_ack_header.seq_num);
}

void send_syn_ack(int sock, uint32_t seq_num, struct sockaddr_in listen_addr)
{
    // Send the SYNACK
    rtp_header_t sent_syn_ack_header;
    sent_syn_ack_header.seq_num = seq_num;
    sent_syn_ack_header.length = 0;
    sent_syn_ack_header.checksum = 0;
    sent_syn_ack_header.flags = RTP_SYN | RTP_ACK;

    // Compute checksum
    sent_syn_ack_header.checksum = compute_checksum(&sent_syn_ack_header, sizeof(rtp_header_t));

    sendto(sock, &sent_syn_ack_header, sizeof(sent_syn_ack_header), 0, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    LOG_DEBUG("Sent RTP_SYNACK with seq_num: %d\n", sent_syn_ack_header.seq_num);
}

void send_fin_ack(int sock, uint32_t seq_num, struct sockaddr_in listen_addr)
{
    // Send the FIN_ACK
    rtp_header_t sent_fin_ack_header;
    sent_fin_ack_header.seq_num = seq_num;
    sent_fin_ack_header.length = 0;
    sent_fin_ack_header.checksum = 0;
    sent_fin_ack_header.flags = RTP_ACK | RTP_FIN;

    // Compute checksum
    sent_fin_ack_header.checksum = compute_checksum(&sent_fin_ack_header, sizeof(rtp_header_t));

    sendto(sock, &sent_fin_ack_header, sizeof(sent_fin_ack_header), 0, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    LOG_DEBUG("Sent RTP_FINACK with seq_num: %d\n", sent_fin_ack_header.seq_num);
}

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        LOG_FATAL("Usage: ./receiver [listen port] [file path] [window size] "
                  "[mode]\n");
    }

    bool is_connected = false;

    std::string listen_port = argv[1];
    std::string file_path = argv[2];
    uint16_t window_size = atoi(argv[3]);
    uint16_t mode = atoi(argv[4]); // 0: RBN, 1: SR

    // Fresh the file
    FILE *file = fopen(file_path.c_str(), "wb");
    fclose(file);

    rtp_header_t *received_syn_header;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        LOG_FATAL("Failed to create socket\n");
    }
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_addr.sin_port = htons(atoi(listen_port.c_str()));
    bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr));

    struct timeval timer;

again:
    timer.tv_sec = 5;
    timer.tv_usec = 0;
    // Wait for 5s to receive the first SYN
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    int ret = select(sock + 1, &readfds, NULL, NULL, &timer);
    if (ret == -1)
    {
        LOG_FATAL("Failed to select\n");
    }
    else if (ret == 0)
    {
        LOG_DEBUG("Timeout, no SYN received\n");
        return 0;
    }
    else
    {
        char buffer[BUFFER_SIZE];
        socklen_t addr_len = sizeof(listen_addr);
        recvfrom(sock, buffer, HEADER_SIZE, 0, (struct sockaddr *)&listen_addr, &addr_len);

        // Convert the buffer to rtp_header_t
        received_syn_header = (rtp_header_t *)buffer;

        uint32_t received_checksum = received_syn_header->checksum;
        received_syn_header->checksum = 0;

        // Check if the checksum is correct
        if (received_checksum != compute_checksum(received_syn_header, sizeof(rtp_header_t)))
        {
            LOG_DEBUG("2: Checksum is incorrect, resending RTP_SYNACK...\n");
            goto again;
        }

        if (received_syn_header->flags == RTP_SYN && received_syn_header->length == 0)
        {
            LOG_DEBUG("Received Correct SYN\n");
            LOG_DEBUG("first_syn_seq_num: %d\n", received_syn_header->seq_num);

            int send_iter_count = 0;
            while (send_iter_count < RESEND_SYN_MAX)
            {
                // Step 2: Send the RTP_SYNACK to the sender
                send_syn_ack(sock, (received_syn_header->seq_num + 1) % MAX_SEQ_NUM, listen_addr);

                send_iter_count++;

                // Step 3: Wait for the ACK from the sender
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(sock, &readfds);

                // Set timeout
                struct timeval timeout;
                timeout.tv_sec = 0;       // 0 seconds
                timeout.tv_usec = 100000; // microseconds -> 100 ms

                int ret = select(sock + 1, &readfds, NULL, NULL, &timeout);

                if (ret == -1) // Select error
                    LOG_FATAL("Failed to select\n");
                else if (ret == 0) // Timeout
                {
                    LOG_DEBUG("Timeout, resending RTP_SYNACK...\n");
                    continue;
                }
                else // Receive the packet successfully
                {
                    char buffer[BUFFER_SIZE];
                    socklen_t addr_len = sizeof(listen_addr);
                    recvfrom(sock, buffer, HEADER_SIZE, 0, (struct sockaddr *)&listen_addr, &addr_len);

                    // Convert the buffer to rtp_header_t
                    rtp_header_t *received_ack_header = (rtp_header_t *)buffer;

                    uint32_t received_ack_checksum = received_ack_header->checksum;
                    received_ack_header->checksum = 0;

                    LOG_DEBUG("received_ack_checksum: %d\n", received_ack_checksum);
                    LOG_DEBUG("compute_checksum: %d\n", compute_checksum(received_ack_header, sizeof(rtp_header_t)));

                    // Check if the checksum is correct
                    if (received_ack_checksum != compute_checksum(received_ack_header, sizeof(rtp_header_t)))
                    {
                        LOG_DEBUG("3: Checksum is incorrect, resending RTP_SYNACK...\n");
                        continue;
                    }
                    if (received_ack_header->flags == RTP_ACK &&
                        received_ack_header->seq_num == received_syn_header->seq_num + 1 &&
                        received_ack_header->length == 0)
                    {
                        LOG_MSG("Received Correct ACK for RTP_SYNACK\n");
                        LOG_MSG("Connection established\n");
                        is_connected = true;
                        break;
                    }
                }
            }
        }
    }

    if (!is_connected)
    {
        LOG_MSG("Failed to establish connection\n");
        LOG_MSG("Receiver: exiting...\n");
        return 0;
    }

    if (mode == 0 || mode == 1)
    {
        if (mode == 0)
            LOG_MSG("Using RBN mode\n");
        else
            LOG_MSG("Using SR mode\n");
        uint32_t base = (received_syn_header->seq_num + 1) % MAX_SEQ_NUM;
        uint32_t exp_seq_num = (received_syn_header->seq_num + 1) % MAX_SEQ_NUM;

        LOG_DEBUG("base: %d, exp_seq_num: %d\n", base, exp_seq_num);

        std::map<uint32_t, bool> window;      // Check if the packet is received
        std::map<uint32_t, std::string> data; // Store the data

        for (int i = 0; i < window_size; i++)
        {
            window[(base + i) % MAX_SEQ_NUM] = false;
        }

        // Print the window
        // for (auto it = window.begin(); it != window.end(); it++)
        // {
        //     LOG_DEBUG("window[%d]: %d\n", it->first, it->second);
        // }

        char buffer[BUFFER_SIZE];
        socklen_t addr_len = sizeof(listen_addr);

        while (true)
        {
            // Receive the data packet
            struct timeval timer;
            timer.tv_sec = 5;
            timer.tv_usec = 0;
            int ret = select(sock + 1, &readfds, NULL, NULL, &timer);
            if (ret == -1)
            {
                LOG_FATAL("Failed to select\n");
            }
            else if (ret == 0)
            {
                LOG_DEBUG("Timeout, no data received\n");
                break;
            }
            else
            {
                recvfrom(sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&listen_addr, &addr_len);

                LOG_DEBUG("buffer size: %ld\n", strlen(buffer));
                LOG_DEBUG("header size: %ld\n", sizeof(rtp_header_t));

                rtp_packet_t received_packet;
                memcpy(&received_packet, buffer, sizeof(rtp_header_t));
                memcpy(received_packet.payload, buffer + sizeof(rtp_header_t), PAYLOAD_MAX);

                uint32_t received_checksum = received_packet.rtp.checksum;
                uint32_t len = received_packet.rtp.length;

                received_packet.rtp.checksum = 0;

                LOG_DEBUG("len of the payload = %d\n", len);

                uint32_t received_seq_num = received_packet.rtp.seq_num % MAX_SEQ_NUM;

                LOG_DEBUG("received_checksum: %d\n", received_checksum);
                LOG_DEBUG("compute_checksum: %d\n", compute_checksum(&received_packet, len + sizeof(rtp_header_t)));

                // Check if the checksum is correct
                if (received_checksum != compute_checksum(&received_packet, len + sizeof(rtp_header_t)))
                {
                    LOG_DEBUG("Checksum is incorrect, wait for the packet again\n");
                    continue;
                }

                LOG_DEBUG("the header received_seq_num: %d, the expected seq_num: %d\n", received_seq_num % MAX_SEQ_NUM, exp_seq_num % MAX_SEQ_NUM);

                // End the Connection
                if (received_seq_num == exp_seq_num && received_packet.rtp.flags == RTP_FIN)
                {
                    int counter = 0;
                    bool end = false;
                    while (counter < RESEND_SYN_MAX)
                    {
                        // Send the FIN_ACK
                        send_fin_ack(sock, exp_seq_num, listen_addr);

                        // Wait for 2s to make sure the receiver has received the FIN_ACK
                        struct timeval timer;
                        timer.tv_sec = 2;
                        timer.tv_usec = 0;

                        int ret_wait = select(sock + 1, &readfds, NULL, NULL, &timer);
                        if (ret_wait == -1) // Select error
                            LOG_FATAL("Failed to select\n");
                        else if (ret_wait == 0) // No FIN_ACK again
                        {
                            LOG_MSG("The sender received FIN_ACK, Ending connection\n");
                            end = true;
                            break;
                        }
                        else // Receive FIN_ACK again
                        {
                            char buffer[BUFFER_SIZE];
                            socklen_t addr_len = sizeof(listen_addr);
                            recvfrom(sock, buffer, HEADER_SIZE, 0, (struct sockaddr *)&listen_addr, &addr_len);
                            continue;
                        }
                    }
                    if (end)
                        break;
                }

                // if (len == 0)
                // {
                //     LOG_DEBUG("Received Incorrect Data Packet -- length = 0\n");
                //     continue;
                // }

                if (received_packet.rtp.flags != 0)
                {
                    LOG_DEBUG("Received Incorrect Data Packet -- flags\n");
                    continue;
                }

                if (window.find(received_seq_num % MAX_SEQ_NUM) == window.end())
                {
                    LOG_DEBUG("The packet is out of the window\n");
                    continue;
                }

                if (window[received_seq_num % MAX_SEQ_NUM] == false)
                {
                    LOG_DEBUG("Received Correct Data Packet\n");
                    // Write the data to the file
                    FILE *file = fopen(file_path.c_str(), "ab");

                    if (file == NULL)
                    {
                        LOG_FATAL("Failed to open file: %s\n", file_path.c_str());
                    }
                    else
                    {
                        LOG_DEBUG("Payload length: %u\n", len);
                        LOG_DEBUG("Payload content: %s\n", received_packet.payload);
                        LOG_DEBUG("Store data in the map\n");
                        data[received_seq_num % MAX_SEQ_NUM] = std::string(received_packet.payload, len);
                        LOG_DEBUG("Data stored in the map successfully\n");
                    }

                    // Update the window
                    window[received_seq_num % MAX_SEQ_NUM] = true;
                    if (received_seq_num == base)
                    {
                        uint32_t i;
                        for (i = 0; i < window_size; i++)
                        {
                            if (window[(base + i) % MAX_SEQ_NUM] == true)
                            {
                                // add a new packet to the window
                                window[(base + window_size + i) % MAX_SEQ_NUM] = false;
                                // erase the old packet
                                window.erase((base + i) % MAX_SEQ_NUM);

                                // Write the data to the file
                                FILE *file = fopen(file_path.c_str(), "ab");

                                size_t written = fwrite(data[(base + i) % MAX_SEQ_NUM].c_str(), 1, data[(base + i) % MAX_SEQ_NUM].length(), file);

                                if (written != data[(base + i) % MAX_SEQ_NUM].length())
                                {
                                    LOG_FATAL("Failed to write data to file. Written: %zu, Expected: %u\n", written, len);
                                }
                                fclose(file);

                                // Erase the data from the map
                                data.erase((base + i) % MAX_SEQ_NUM);
                            }
                            else
                                break;
                        }
                        base = (base + i) % MAX_SEQ_NUM;
                        exp_seq_num = (exp_seq_num + i) % MAX_SEQ_NUM;

                        LOG_DEBUG("Update the window, now base = %d, exp_seq_num = %d\n", base, exp_seq_num);

                        // Print the window
                        // for (auto it = window.begin(); it != window.end(); it++)
                        // {
                        //     LOG_DEBUG("window[%d]: %d\n", it->first, it->second);
                        // }
                    }

                    // Send ACK
                    if (mode == 0)
                    {
                        // Send ACK for RBN
                        send_ack(sock, exp_seq_num, listen_addr);
                    }
                    else
                    {
                        // Send ACK for SR
                        send_ack(sock, received_seq_num, listen_addr);
                    }
                }
                else // Received the same packet again
                {
                    LOG_DEBUG("Received the same packet again\n");
                    LOG_DEBUG("Sending ACK again\n");
                    if (mode == 0)
                    {
                        // Send ACK for RBN
                        send_ack(sock, exp_seq_num, listen_addr);
                    }
                    else
                    {
                        // Send ACK for SR
                        send_ack(sock, received_seq_num, listen_addr);
                    }
                }
            }
        }
        LOG_MSG("File received successfully\n");
    }
    else
    {
        LOG_FATAL("Invalid mode\n");
    }

    close(sock);

    return 0;
}
