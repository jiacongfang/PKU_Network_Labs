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
#define RESEND_SYN_MAX 50
#define BUFFER_SIZE 2048

void send_syn(int sock, uint32_t seq_num, struct sockaddr_in listen_addr)
{
    // Send the SYN
    rtp_header_t sent_syn_ack_header;
    sent_syn_ack_header.seq_num = seq_num;
    sent_syn_ack_header.length = 0;
    sent_syn_ack_header.checksum = 0;
    sent_syn_ack_header.flags = RTP_SYN;

    // Compute checksum
    sent_syn_ack_header.checksum = compute_checksum(&sent_syn_ack_header, sizeof(rtp_header_t));

    sendto(sock, &sent_syn_ack_header, sizeof(sent_syn_ack_header), 0, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    LOG_DEBUG("Sent RTP_SYN with seq_num: %d\n", sent_syn_ack_header.seq_num);
}

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

void send_packet_rbn(int sock, uint32_t seq_num, struct sockaddr_in listen_addr, std::map<uint32_t, bool> &window, std::map<uint32_t, std::string> &data, uint32_t base, uint32_t next_seq_num, FILE *file)
{
    // Send the packet
    rtp_packet_t sent_packet;
    sent_packet.rtp.seq_num = next_seq_num;
    sent_packet.rtp.length = fread(sent_packet.payload, 1, PAYLOAD_MAX, file);
    sent_packet.rtp.checksum = 0;
    sent_packet.rtp.flags = 0;

    // Compute checksum
    sent_packet.rtp.checksum = compute_checksum(&sent_packet, sent_packet.rtp.length + sizeof(rtp_header_t));

    // Send the packet
    sendto(sock, &sent_packet, sent_packet.rtp.length + sizeof(rtp_header_t), 0, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    LOG_DEBUG("Sent RTP_PACKET with seq_num: %d\n", sent_packet.rtp.seq_num);

    LOG_DEBUG("length of the payload = %d\n", sent_packet.rtp.length);

    LOG_DEBUG("Payload: %s\n", sent_packet.payload);

    // Update the window
    window[next_seq_num] = true;
    data[next_seq_num] = std::string(sent_packet.payload, sent_packet.rtp.length);
}

void send_stored_data(int sock, struct sockaddr_in listen_addr, std::map<uint32_t, bool> &window, std::map<uint32_t, std::string> &data, uint32_t base, uint32_t next_seq_num)
{
    // Send the stored data
    rtp_packet_t sent_packet;
    sent_packet.rtp.seq_num = next_seq_num;
    sent_packet.rtp.length = data[next_seq_num].length();
    memcpy(sent_packet.payload, data[next_seq_num].c_str(), data[next_seq_num].length());
    sent_packet.rtp.checksum = 0;
    sent_packet.rtp.flags = 0;

    // Compute checksum
    sent_packet.rtp.checksum = compute_checksum(&sent_packet, sent_packet.rtp.length + sizeof(rtp_header_t));

    // Send the packet
    sendto(sock, &sent_packet, sent_packet.rtp.length + sizeof(rtp_header_t), 0, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    LOG_DEBUG("Resent RTP_PACKET with seq_num: %d, the length is %d\n", sent_packet.rtp.seq_num, sent_packet.rtp.length);

    LOG_DEBUG("Payload: %s\n", sent_packet.payload);
}

void send_fin(int sock, uint32_t seq_num, struct sockaddr_in listen_addr)
{
    // Send the FIN
    rtp_header_t sent_fin_header;
    sent_fin_header.seq_num = seq_num;
    sent_fin_header.length = 0;
    sent_fin_header.checksum = 0;
    sent_fin_header.flags = RTP_FIN;

    // Compute checksum
    sent_fin_header.checksum = compute_checksum(&sent_fin_header, sizeof(rtp_header_t));

    sendto(sock, &sent_fin_header, sizeof(sent_fin_header), 0, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    LOG_DEBUG("Sent RTP_FIN with seq_num: %d\n", sent_fin_header.seq_num);
}

/*
    Usage: ./sender [receiver ip] [receiver port] [file path] [window size] [mode]
*/
int main(int argc, char **argv)
{
    if (argc != 6)
    {
        LOG_FATAL("Usage: ./sender [receiver ip] [receiver port] [file path] "
                  "[window size] [mode]\n");
    }

    bool is_connected = false;

    std::string receiver_ip = argv[1];
    uint16_t receiver_port = atoi(argv[2]);
    std::string file_path = argv[3];
    uint32_t window_size = atoi(argv[4]);
    uint16_t mode = atoi(argv[5]); // 0: RBN, 1: SR

    // Build the connection
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        LOG_FATAL("Failed to create socket\n");
    }

    struct sockaddr_in receiver_addr;
    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    inet_pton(AF_INET, receiver_ip.c_str(), &receiver_addr.sin_addr);
    receiver_addr.sin_port = htons(receiver_port);

    int send_iter_count = 0;

    // set a random `seq_num`
    uint32_t init_seq_num = rand() % MAX_SEQ_NUM;

    while (send_iter_count < RESEND_SYN_MAX)
    {
        // Step 1: Send RTP_SYN,
        send_syn(sockfd, init_seq_num, receiver_addr);

        // Step 2. Wait for ACK
        // 设置一个文件描述符集合, 用于后续的select调用
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 0;       // 0 seconds
        timeout.tv_usec = 100000; // microseconds -> 100 ms

        int ret = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (ret == -1) // Select error
            LOG_FATAL("Failed to select\n");
        else if (ret == 0) // Timeout
        {
            LOG_DEBUG("Timeout, resending RTP_SYN...\n");
            send_iter_count++;
            continue;
        }
        else // Receive the packet successfully
        {
            char buffer[BUFFER_SIZE];
            socklen_t addr_len = sizeof(receiver_addr);
            recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&receiver_addr, &addr_len);

            // Convert the buffer to rtp_header_t
            rtp_header_t *recievied_ack_header = (rtp_header_t *)buffer;

            uint32_t received_checksum = recievied_ack_header->checksum;
            recievied_ack_header->checksum = 0;

            // Check if the checksum is correct
            if (received_checksum != compute_checksum(recievied_ack_header, sizeof(rtp_header_t)))
            {
                LOG_DEBUG("Checksum is incorrect, resending RTP_SYN...\n");
                send_iter_count++;
                continue;
            }

            if (recievied_ack_header->flags == (RTP_ACK | RTP_SYN) && recievied_ack_header->seq_num == init_seq_num + 1)
            {
                LOG_DEBUG("Received Correct ACK for RTP_SYN\n");

                while (true)
                {
                    // Step 3: Send the RTP_ACK to the receiver
                    send_ack(sockfd, recievied_ack_header->seq_num, receiver_addr);

                    // Wait for 2s to make sure the receiver has received the ACK
                    // If received the SYNACK pkt again, resend the ACK
                    struct timeval timer;
                    timer.tv_sec = 2;
                    timer.tv_usec = 0;

                    int ret_wait = select(sockfd + 1, &readfds, NULL, NULL, &timer);
                    if (ret_wait == -1) // Select error
                        LOG_FATAL("Failed to select\n");
                    else if (ret_wait == 0) // No SYNACK again
                    {
                        LOG_MSG("No more SYNACK received\n");
                        send_iter_count++;
                        break;
                    }
                    else // Receive again -> resend ACK
                    {
                        // 读取数据后丢弃
                        char buffer[BUFFER_SIZE];
                        socklen_t addr_len = sizeof(receiver_addr);
                        recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&receiver_addr, &addr_len);
                        continue;
                    }
                }
                LOG_MSG("Connection established\n");
                is_connected = true;
                break;
            }
        }
    }

    if (!is_connected)
    {
        LOG_FATAL("Failed to establish connection\n");
        LOG_DEBUG("Sender: exiting...\n");
        return 0;
    }

    // Start sending the file
    FILE *file = fopen(file_path.c_str(), "rb");
    if (file == NULL)
    {
        LOG_FATAL("Failed to open file\n");
    }

    // Get the file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size == 0)
    {
        LOG_FATAL("Empty file\n");
    }

    // Calculate the number of packets
    uint16_t num_packets = file_size / PAYLOAD_MAX + 1;

    if (mode == 0) // RBN
    {
        LOG_DEBUG("RBN mode\n");
        u_int32_t base = (init_seq_num + 1) % MAX_SEQ_NUM;
        u_int32_t next_seq_num = (init_seq_num + 1) % MAX_SEQ_NUM;

        // Init the window
        std::map<u_int32_t, bool> window;      // Check if the packet is sent before
        std::map<u_int32_t, std::string> data; // Include the header and payload
        for (u_int32_t i = 0; i < window_size; i++)
        {
            window[(base + i) % MAX_SEQ_NUM] = false;
        }

        // Loop until the file is sent
        while (true)
        {
            // if file is sent, break
            if (num_packets == 0)
            {
                break;
            }

            // Send the entire window
            for (u_int32_t i = 0; i < window_size; i++)
            {
                if (window.find((base + i) % MAX_SEQ_NUM) == window.end())
                {
                    LOG_FATAL("Failed to find the packet in the window\n");
                }

                if (window[(base + i) % MAX_SEQ_NUM] == false)
                {
                    send_packet_rbn(sockfd, base + i, receiver_addr, window, data, base, (base + i) % MAX_SEQ_NUM, file);
                    next_seq_num = (next_seq_num + 1) % MAX_SEQ_NUM;

                    // Expand the window
                    window[(base + window_size + i) % MAX_SEQ_NUM] = false;
                }

                if (window[(base + i) % MAX_SEQ_NUM] == true)
                {
                    send_stored_data(sockfd, receiver_addr, window, data, base, (base + i) % MAX_SEQ_NUM);

                    window[(base + window_size + i) % MAX_SEQ_NUM] = false;
                }
            }

            // Print the window
            // for (auto it = window.begin(); it != window.end(); it++)
            // {
            //     LOG_DEBUG("window[%d]: %d\n", it->first, it->second);
            // }

        wait_ack:
            // Wait for ACK, timeout is 100ms
            struct timeval timer;
            timer.tv_sec = 0;
            timer.tv_usec = 100000;

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);

            int ret = select(sockfd + 1, &readfds, NULL, NULL, &timer);
            if (ret == -1) // Select error
                LOG_FATAL("Failed to select\n");
            else if (ret == 0) // Timeout
            {
                LOG_DEBUG("Timeout, resending the window...\n");
                continue;
            }
            else // Receive the packet successfully
            {
                LOG_DEBUG("Receive a 'seem' ACK\n");
                char buffer[BUFFER_SIZE];
                socklen_t addr_len = sizeof(receiver_addr);
                recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&receiver_addr, &addr_len);

                // Convert the buffer to rtp_header_t
                rtp_header_t *received_ack_header = (rtp_header_t *)buffer;
                uint32_t received_checksum = received_ack_header->checksum;
                received_ack_header->checksum = 0;

                // Check if the checksum is correct
                if (received_checksum != compute_checksum(received_ack_header, sizeof(rtp_header_t)))
                {
                    LOG_DEBUG("Checksum is incorrect, resending the window...\n");
                    goto wait_ack;
                }

                if (received_ack_header->flags != RTP_ACK)
                {
                    LOG_DEBUG("Received Incorrect ACK Packet\n");
                    goto wait_ack;
                }

                if (window.find(received_ack_header->seq_num) == window.end())
                {
                    LOG_DEBUG("Received seq_sum: %d\n", received_ack_header->seq_num);
                    LOG_DEBUG("The ACK is out of the window\n");
                    goto wait_ack;
                }
                else
                {
                    LOG_DEBUG("Received Correct ACK Packet\n");
                    // update the window
                    while (base <= received_ack_header->seq_num)
                    {
                        LOG_DEBUG("received_ack_header->seq_num: %d\n", received_ack_header->seq_num);
                        window.erase(base);
                        base = (base + 1) % MAX_SEQ_NUM;
                        window[(base + window_size - 1) % MAX_SEQ_NUM] = false;
                        num_packets--;

                        if (num_packets == 0)
                        {
                            goto end;
                        }

                        // send the next packet
                        send_packet_rbn(sockfd, (base + window_size - 1) % MAX_SEQ_NUM, receiver_addr, window, data, base, (base + window_size - 1) % MAX_SEQ_NUM, file);

                        next_seq_num = (next_seq_num + 1) % MAX_SEQ_NUM;

                        // Print the window
                        // for (auto it = window.begin(); it != window.end(); it++)
                        // {
                        //     LOG_DEBUG("window[%d]: %d\n", it->first, it->second);
                        // }

                        goto wait_ack;
                    }
                }
            }
        }
    end:
        // close the file
        fclose(file);

        uint32_t counter = 0;
        while (counter < RESEND_SYN_MAX)
        {
            // Send the FIN
            send_fin(sockfd, next_seq_num, receiver_addr);

            // Wait for the FIN_ACK
            struct timeval timer;
            timer.tv_sec = 0;
            timer.tv_usec = 100000;

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sockfd, &readfds);

            int ret = select(sockfd + 1, &readfds, NULL, NULL, &timer);

            if (ret == -1) // Select error
                LOG_FATAL("Failed to select\n");
            else if (ret == 0) // Timeout
            {
                LOG_MSG("No FIN_ACK received, resending FIN...\n");
                send_fin(sockfd, next_seq_num, receiver_addr);
                counter++;
                continue;
            }
            else
            {
                char buffer[BUFFER_SIZE];
                socklen_t addr_len = sizeof(receiver_addr);
                recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&receiver_addr, &addr_len);

                // Convert the buffer to rtp_header_t
                rtp_header_t *received_fin_ack_header = (rtp_header_t *)buffer;
                uint32_t received_checksum = received_fin_ack_header->checksum;
                received_fin_ack_header->checksum = 0;

                // Check if the checksum is correct
                if (received_checksum != compute_checksum(received_fin_ack_header, sizeof(rtp_header_t)))
                {
                    LOG_DEBUG("Checksum is incorrect, resending FIN...\n");
                    send_fin(sockfd, next_seq_num, receiver_addr);
                    counter++;
                    continue;
                }

                if (received_fin_ack_header->flags != (RTP_ACK | RTP_FIN))
                {
                    LOG_DEBUG("Received Incorrect FIN_ACK Packet\n");
                    continue;
                }

                if (received_fin_ack_header->seq_num == next_seq_num)
                {
                    LOG_MSG("Received FIN_ACK, Ending connection\n");
                    break;
                }

                // Wait for 2s to make sure the receiver has received the FIN_ACK

                struct timeval timer;
                timer.tv_sec = 2;
                timer.tv_usec = 0;

                int ret_wait = select(sockfd + 1, &readfds, NULL, NULL, &timer);
                if (ret_wait == -1) // Select error
                    LOG_FATAL("Failed to select\n");
                else if (ret_wait == 0) // No FIN_ACK again
                {
                    LOG_MSG("The sender received FIN_ACK, Ending connection\n");
                    break;
                }
                else // Receive FIN_ACK again
                {
                    char buffer[BUFFER_SIZE];
                    socklen_t addr_len = sizeof(receiver_addr);
                    recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&receiver_addr, &addr_len);
                    continue;
                }
            }
        }
    }

    LOG_MSG("File sent successfully\n");
    LOG_DEBUG("Sender: exiting...\n");

    return 0;
}
