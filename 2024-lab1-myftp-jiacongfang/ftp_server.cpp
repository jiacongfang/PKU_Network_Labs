#include "ftp_head.h"

void *client_handler(void *client)
{
    std::string current_dir = std::filesystem::current_path();
    int client_sock = *(int *)client;

    std::cout << 1 << std::endl;

    // Receive OPEN_CONN_REQUEST
    char buffer[MAX_BUFFER_SIZE];
    if (safe_recv(client_sock, buffer, sizeof(OPEN_CONN_REQUEST)) < 0)
    {
        std::cout << "Failed to receive open connection request" << std::endl;
        close(client_sock);
        return NULL;
    }
    struct data_packet received_open_request;

    memcpy(&received_open_request, buffer, sizeof(OPEN_CONN_REQUEST));
    if (memcmp(received_open_request.m_protocol, OPEN_CONN_REQUEST.m_protocol, MAGIC_NUMBER_LENGTH) != 0 ||
        received_open_request.m_type != OPEN_CONN_REQUEST.m_type ||
        received_open_request.m_length != OPEN_CONN_REQUEST.m_length)
    {
        std::cout << "Invalid open connection request" << std::endl;
        std::cout << "received_open_request.m_protocol: " << received_open_request.m_protocol << std::endl;
        std::cout << "Connection closed" << std::endl;
        close(client_sock);
        goto quit;
    }

    // Send OPEN_CONN_REPLY
    if (sizeof(OPEN_CONN_REPLY) != safe_send(client_sock, (char *)&OPEN_CONN_REPLY, sizeof(OPEN_CONN_REPLY)))
    {
        std::cout << "Failed to send open connection reply" << std::endl;
        std::cout << "Connection closed" << std::endl;
        close(client_sock);
        goto quit;
    }

    std::cout << "Connection established" << std::endl;

    while (true)
    {
        // Receive REQUEST
        char request_buffer[MAX_BUFFER_SIZE];
        struct data_packet received_request;
        size_t i = safe_recv(client_sock, request_buffer, sizeof(OPEN_CONN_REQUEST));
        if (i < 0)
        {
            std::cout << "Failed to receive request" << std::endl;
            std::cout << "Connection closed" << std::endl;
            close(client_sock);
            goto quit;
        }
        std::cout << "Received " << i << " bytes" << std::endl;
        std::cout << std::hex << std::setw(2) << request_buffer << std::endl;
        memcpy(&received_request, request_buffer, sizeof(OPEN_CONN_REQUEST));

        print_request(received_request, std::string("Received REQUEST:"));
        print_request(OPEN_CONN_REQUEST, std::string("Stand REQUEST:"));

        if (memcmp(received_request.m_protocol, OPEN_CONN_REQUEST.m_protocol, MAGIC_NUMBER_LENGTH) != 0)
        {
            std::cout << "Invalid protocol" << std::endl;
            std::cout << "Connection closed" << std::endl;
            close(client_sock);
            break;
        }
        print_request(received_request, std::string("Received REQUEST:"));

        // 2. LIST
        if (received_request.m_type == LIST_REQUEST.m_type)
        {
            std::string command = "ls " + std::string(current_dir);
            FILE *fp = popen(command.c_str(), "r");
            if (fp == NULL)
            {
                std::cout << "Failed to execute 'ls -r' command" << std::endl;
                close(client_sock);
                goto quit;
            }
            char list_content[MAX_BUFFER_SIZE];
            size_t length = fread(list_content, 1, MAX_BUFFER_SIZE, fp);
            pclose(fp);
            std::cout << length << std::endl;
            list_content[length] = '\0';

            struct data_packet list_reply = {
                .m_protocol = PROTOCOL,
                .m_type = LIST_REPLY.m_type,
                .m_status = 0,
                .m_length = htonl(12 + length + 1)};

            // Send LIST_REPLY Header
            if (sizeof(list_reply) != safe_send(client_sock, (char *)&list_reply, sizeof(list_reply)))
            {
                std::cout << "Failed to send list reply" << std::endl;
                close(client_sock);
                goto quit;
            }
            // Send LIST_REPLY Payload
            if (length + 1 != safe_send(client_sock, list_content, length + 1))
            {
                std::cout << "Failed to send list content" << std::endl;
                close(client_sock);
                goto quit;
            }
            std::cout << "List messages below are sent" << std::endl;
            print_request(list_reply, std::string("LIST_REPLY:"));
            std::cout << list_content << std::endl;
        }
        // 3. CD
        else if (received_request.m_type == CD_REQUEST.m_type)
        {
            size_t length = ntohl(received_request.m_length) - 12;
            std::cout << "Length: " << length << std::endl;
            // Receive directory
            char dir[MAX_BUFFER_SIZE];
            if (safe_recv(client_sock, dir, length) < 0)
            {
                std::cout << "Failed to receive directory" << std::endl;
                close(client_sock);
                goto quit;
            }
            std::cout << "Received directory: " << dir << std::endl;
            dir[length - 1] = '\0';
            std::cout << "Change directory to " << dir << std::endl;

            // Set Current Directory as dir
            std::filesystem::path current_fs_path = current_dir;
            std::filesystem::path dic_path = current_fs_path / dir;
            std::filesystem::path absolute_path = std::filesystem::absolute(dic_path);

            if (!std::filesystem::exists(absolute_path) || !std::filesystem::is_directory(absolute_path))
            {
                std::cout << "Directory '" << absolute_path << "' does not exist" << std::endl;

                // Send CD_REPLY_FAIL
                struct data_packet cd_reply_fail =
                    {
                        .m_protocol = PROTOCOL,
                        .m_type = CD_REPLY_FAIL.m_type,
                        .m_status = 0,
                        .m_length = htonl(12)};

                if (sizeof(cd_reply_fail) != safe_send(client_sock, (char *)&cd_reply_fail, sizeof(cd_reply_fail)))
                {
                    std::cout << "Failed to send cd reply fail" << std::endl;
                    close(client_sock);
                    goto quit;
                }
                std::cout << "CD reply fail messages sent" << std::endl;
                continue;
            }

            // Send CD_REPLY_SUCCESS
            struct data_packet cd_reply_success =
                {
                    .m_protocol = PROTOCOL,
                    .m_type = CD_REPLY_SUCCESS.m_type,
                    .m_status = 1,
                    .m_length = htonl(12)};

            if (sizeof(cd_reply_success) != safe_send(client_sock, (char *)&cd_reply_success, sizeof(cd_reply_success)))
            {
                std::cout << "Failed to send cd reply success" << std::endl;
                close(client_sock);
                goto quit;
            }
            print_request(cd_reply_success, std::string("CD_REPLY_SUCCESS:"));

            // Update current directory
            current_dir = absolute_path.string();
            std::cout << "Current working directory changed to: " << current_dir << std::endl;
        }
        // 4. GET FILE
        else if (received_request.m_type == GET_REQUEST.m_type)
        {
            size_t length = ntohl(received_request.m_length) - 12;
            std::cout << "Length: " << length << std::endl;

            char file_name[MAX_BUFFER_SIZE];
            if (safe_recv(client_sock, file_name, length) < 0)
            {
                std::cout << "Failed to receive file name" << std::endl;
                close(client_sock);
                goto quit;
            }

            std::cout << "Received file name: " << file_name << std::endl;

            std::filesystem::path fs_path = current_dir;
            std::filesystem::path file_path = fs_path / file_name;

            // File does not exist
            if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path))
            {
                std::cout << "File '" << file_path << "' does not exist" << std::endl;

                // Send GET_REPLY_FAIL
                struct data_packet get_reply_fail =
                    {
                        .m_protocol = PROTOCOL,
                        .m_type = GET_REPLY_FAIL.m_type,
                        .m_status = 0,
                        .m_length = htonl(12)};

                if (sizeof(get_reply_fail) != safe_send(client_sock, (char *)&get_reply_fail, sizeof(get_reply_fail)))
                {
                    std::cout << "Failed to send get reply fail" << std::endl;
                    close(client_sock);
                    goto quit;
                }
                std::cout << "GET reply fail messages sent" << std::endl;
                continue;
            }

            // Send GET_REPLY_SUCCESS
            struct data_packet get_reply_success =
                {
                    .m_protocol = PROTOCOL,
                    .m_type = GET_REPLY_SUCCESS.m_type,
                    .m_status = 1,
                    .m_length = htonl(12)};
            if (sizeof(get_reply_success) != safe_send(client_sock, (char *)&get_reply_success, sizeof(get_reply_success)))
            {
                std::cout << "Failed to send get reply success" << std::endl;
                close(client_sock);
                goto quit;
            }
            print_request(get_reply_success, std::string("GET_REPLY_SUCCESS:"));

            // Send FILE_DATA
            std::ifstream file(file_path, std::ios::binary);
            if (!file)
            {
                std::cout << "Failed to open file" << std::endl;
                close(client_sock);
                goto quit;
            }
            size_t file_size = std::filesystem::file_size(file_path);
            char *file_content = new char[file_size];
            file.read(file_content, file_size);
            if (!file)
            {
                std::cout << "Failed to read file" << std::endl;
                close(client_sock);
                goto quit;
            }
            file.close();

            // Send File Data Header
            struct data_packet file_data =
                {
                    .m_protocol = PROTOCOL,
                    .m_type = FILE_DATA.m_type,
                    .m_status = 0,
                    .m_length = htonl(12 + file_size)};
            if (sizeof(file_data) != safe_send(client_sock, (char *)&file_data, sizeof(file_data)))
            {
                std::cout << "Failed to send file data" << std::endl;
                close(client_sock);
                goto quit;
            }

            // Send File Data Payload
            if (file_size != safe_send(client_sock, file_content, file_size))
            {
                std::cout << "Failed to send file content" << std::endl;
                close(client_sock);
                goto quit;
            }
            std::cout << "File data messages sent" << std::endl;
        }
        // 5. PUT FILE
        else if (received_request.m_type == PUT_REQUEST.m_type)
        {
            size_t length = ntohl(received_request.m_length) - 12;
            std::cout << "Length: " << length << std::endl;
            char file_name[MAX_BUFFER_SIZE];
            if (safe_recv(client_sock, file_name, length) < 0)
            {
                std::cout << "Failed to receive file name" << std::endl;
                close(client_sock);
                goto quit;
            }
            std::cout << "Received file name: " << file_name << std::endl;
            file_name[length - 1] = '\0';

            // Send PUT_REPLY
            struct data_packet put_reply =
                {
                    .m_protocol = PROTOCOL,
                    .m_type = PUT_REPLY.m_type,
                    .m_status = 0,
                    .m_length = htonl(12)};
            if (sizeof(put_reply) != safe_send(client_sock, (char *)&put_reply, sizeof(put_reply)))
            {
                std::cout << "Failed to send put reply" << std::endl;
                close(client_sock);
                goto quit;
            }
            print_request(put_reply, std::string("PUT_REPLY:"));

            // Receive FILE_DATA Header
            char file_data[MAX_BUFFER_SIZE];
            struct data_packet file_data_packet;
            if (safe_recv(client_sock, file_data, sizeof(file_data_packet)) < 0)
            {
                std::cout << "Failed to receive file data" << std::endl;
                close(client_sock);
                goto quit;
            }

            memcpy(&file_data_packet, file_data, sizeof(file_data_packet));

            print_request(file_data_packet, std::string("FILE_DATA:"));

            size_t file_size = ntohl(file_data_packet.m_length) - 12;

            std::cout << "file_size: " << file_size << std::endl;

            // Receive FILE_DATA Payload
            char *file_content = new char[file_size];
            if (safe_recv(client_sock, file_content, file_size) < 0)
            {
                std::cout << "Failed to receive file content" << std::endl;
                close(client_sock);
                goto quit;
            }
            std::cout << file_content << std::endl;

            // Write to file
            std::filesystem::path fs_path = current_dir;
            std::filesystem::path file_path = fs_path / file_name;

            if (std::filesystem::exists(file_path))
                std::cout << "File '" << file_name << "' already exists. Will Cover it" << std::endl;

            std::ofstream file(file_path, std::ios::binary);
            if (!file)
            {
                std::cout << "Failed to open file" << std::endl;
                close(client_sock);
                goto quit;
            }
            file.write(file_content, file_size);
            if (!file)
            {
                std::cout << "Failed to write file" << std::endl;
                close(client_sock);
                goto quit;
            }
            file.close();
            std::cout << "File '" << file_name << "' has been saved in" << file_path << std::endl;
        }
        // 6. SHA
        else if (received_request.m_type == SHA_REQUEST.m_type)
        {
            size_t length = ntohl(received_request.m_length) - 12;
            char file_name[MAX_BUFFER_SIZE];
            if (safe_recv(client_sock, file_name, length) < 0)
            {
                std::cout << "Failed to receive file name" << std::endl;
                close(client_sock);
                goto quit;
            }

            std::cout << "sha256 request for file: " << file_name << std::endl;

            std::filesystem::path fs_path = current_dir;
            std::filesystem::path file_path = fs_path / file_name;

            // File does not exist
            if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path))
            {
                std::cout << "File '" << file_path << "' does not exist" << std::endl;

                // Send SHA_REPLY_FAIL
                struct data_packet sha_reply_fail =
                    {
                        .m_protocol = PROTOCOL,
                        .m_type = SHA_REPLY_FAIL.m_type,
                        .m_status = 0,
                        .m_length = htonl(12)};
                if (sizeof(sha_reply_fail) != safe_send(client_sock, (char *)&sha_reply_fail, sizeof(sha_reply_fail)))
                {
                    std::cout << "Failed to send sha reply fail" << std::endl;
                    close(client_sock);
                    goto quit;
                }
                std::cout << "SHA reply fail messages sent" << std::endl;
                continue;
            }

            // Send SHA_REPLY_SUCCESS
            struct data_packet sha_reply_success =
                {
                    .m_protocol = PROTOCOL,
                    .m_type = SHA_REPLY_SUCCESS.m_type,
                    .m_status = 1,
                    .m_length = htonl(12)};
            if (sizeof(sha_reply_success) != safe_send(client_sock, (char *)&sha_reply_success, sizeof(sha_reply_success)))
            {
                std::cout << "Failed to send sha reply success" << std::endl;
                close(client_sock);
                goto quit;
            }

            // Calculate SHA256
            std::string command = "sha256sum " + file_path.string();
            FILE *fp = popen(command.c_str(), "r");
            if (fp == NULL)
            {
                std::cout << "Failed to execute 'sha256sum FILE' command" << std::endl;
                close(client_sock);
                goto quit;
            }
            char sha_content[MAX_BUFFER_SIZE];
            size_t sha_size = fread(sha_content, 1, MAX_BUFFER_SIZE, fp);
            pclose(fp);
            sha_content[sha_size] = '\0';

            struct data_packet sha_data =
                {
                    .m_protocol = PROTOCOL,
                    .m_type = FILE_DATA.m_type,
                    .m_status = 0,
                    .m_length = htonl(12 + sha_size + 1)};

            // Send SHA Data Header
            if (sizeof(sha_data) != safe_send(client_sock, (char *)&sha_data, sizeof(sha_data)))
            {
                std::cout << "Failed to send sha data" << std::endl;
                close(client_sock);
                goto quit;
            }
            // Send SHA Data Payload
            if (sha_size + 1 != safe_send(client_sock, sha_content, sha_size + 1))
            {
                std::cout << "Failed to send sha content" << std::endl;
                close(client_sock);
                goto quit;
            }
            std::cout << "SHA data messages sent" << std::endl;
            print_request(sha_data, std::string("SHA_DATA:"));
            std::cout << sha_content << std::endl;
        }
        // 7. Quit
        else if (received_request.m_type == QUIT_REQUEST.m_type)
        {
            char quit_reply[MAX_BUFFER_SIZE];
            memcpy(quit_reply, &QUIT_REPLY, sizeof(QUIT_REPLY));
            if (sizeof(QUIT_REPLY) != safe_send(client_sock, quit_reply, sizeof(QUIT_REPLY)))
            {
                std::cout << "Failed to send quit reply" << std::endl;
                close(client_sock);
                goto quit;
            }
            std::cout << "Quit reply messages sent" << std::endl;
            close(client_sock);
            goto quit;
        }
        // Invalid request
        else
        {
            std::cout << "Invalid request" << std::endl;
            close(client_sock);
            goto quit;
        }
    }
quit:
    pthread_exit(0);
}

// ./ftp_server IP PORT
int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cout << "Usage: " << argv[0] << " <IP> <PORT>" << std::endl;
        return -1;
    }
    std::string ip = argv[1];
    std::string port = argv[2];
    std::cout << "Server started at " << ip << ":" << port << std::endl;

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0)
    {
        std::cout << "Failed to create socket" << std::endl;
        return -1;
    }
    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(std::stoi(port));
    inet_pton(AF_INET, ip.c_str(), &listen_addr.sin_addr);

    if (0 > bind(listen_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)))
    {
        std::cout << "Failed to bind" << std::endl;
        close(listen_sock);
        return -1;
    }

    if (0 > listen(listen_sock, 128))
    {
        std::cout << "Failed to listen" << std::endl;
        close(listen_sock);
        return -1;
    }

    while (true)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, (socklen_t *)&client_len);
        if (client_sock < 0)
        {
            std::cout << "Failed to accept" << std::endl;
            close(listen_sock);
            return -1;
        }

        std::cout << "Client Connection: " << inet_ntoa(client_addr.sin_addr)
                  << ":" << ntohl(client_addr.sin_port) << std::endl;

        int *p_client = new int;
        *p_client = client_sock;

        pthread_t thread;

        if (pthread_create(&thread, NULL, client_handler, (void *)p_client) != 0)
        {
            std::cout << "Failed to create thread" << std::endl;
            close(client_sock);
            close(listen_sock);
            return -1;
        }
        else
        {
            pthread_detach(thread);
        }
    }

    close(listen_sock);
    std::cout << "Server closed" << std::endl;
    return 0;
}