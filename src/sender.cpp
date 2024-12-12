#include "rtp.h"
#include "util.h"
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

// Macros
#define RESEND_SYN_MAX 50
#define BUFFER_SIZE 2048

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
    rtp_header_t syn_header;
    syn_header.seq_num = rand();
    syn_header.length = 0;
    syn_header.checksum = 0;
    syn_header.flags = RTP_SYN;

    // Compute checksum
    syn_header.checksum = compute_checksum(&syn_header, sizeof(syn_header));

    while (send_iter_count < RESEND_SYN_MAX)
    {
        // Step 1: Send RTP_SYN,
        sendto(sockfd, &syn_header, sizeof(syn_header), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
        LOG_DEBUG("Sent RTP_SYN with seq_num: %d\n", syn_header.seq_num);

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

            if (recievied_ack_header->flags == (RTP_ACK | RTP_SYN) && recievied_ack_header->seq_num == syn_header.seq_num + 1)
            {
                LOG_DEBUG("Received Correct ACK for RTP_SYN\n");

                while (true)
                {
                    // Step 3: Send the RTP_ACK to the receiver
                    rtp_header_t sent_ack_header;
                    sent_ack_header.seq_num = recievied_ack_header->seq_num + 1;
                    sent_ack_header.length = 0;
                    sent_ack_header.checksum = 0;
                    sent_ack_header.flags = RTP_ACK;

                    // Compute checksum
                    sent_ack_header.checksum = compute_checksum(&sent_ack_header, sizeof(sent_ack_header));

                    sendto(sockfd, &sent_ack_header, sizeof(sent_ack_header), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
                    LOG_DEBUG("Sent RTP_ACK with seq_num: %d\n", sent_ack_header.seq_num);

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
    uint16_t num_packets = file_size / PAYLOAD_MAX;

    if (mode == 0) // RBN
    {
        LOG_DEBUG("RBN mode\n");
        u_int32_t base = syn_header.seq_num + 1;
        u_int32_t next_seq_num = base;

        // Set timer
        struct timeval timer;
        timer.tv_sec = 2;
        timer.tv_usec = 0;

        // window size has defined at the beginning

        // Send the first window
        for (uint32_t i = 0; i < window_size; i++)
        {
            rtp_packet_t packet;
            packet.rtp.seq_num = next_seq_num;
            packet.rtp.length = fread(packet.payload, 1, PAYLOAD_MAX, file);
            packet.rtp.checksum = 0;
            packet.rtp.flags = 0;

            // Compute checksum
            packet.rtp.checksum = compute_checksum(&packet.rtp, sizeof(packet.rtp));

            sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
            LOG_DEBUG("Sent RTP packet with seq_num: %d\n", packet.rtp.seq_num);

            next_seq_num++;
        }
        // Start the timer
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timer, sizeof(timer));
    }
}
