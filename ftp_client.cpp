#include "ftp_head.h"

static std::string info = "none";

// Check the connection status
bool check_server_status(int &sock)
{
    int error = 0;
    socklen_t len = sizeof(error);
    int retval = getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);

    if (retval != 0 || error != 0)
    {
        std::cout << "Server is down" << std::endl;
        close(sock);
        sock = -1;
        info = "none";
        return false;
    }
    return true;
}

int open_connection(int &sock, std::string server_ip, std::string server_port)
{
    char open_request[MAX_BUFFER_SIZE];
    if (sizeof(OPEN_CONN_REQUEST) > MAX_BUFFER_SIZE)
    {
        std::cout << "Buffer size too small" << std::endl;
        close(sock);
        sock = -1;
        return -1;
    }
    memcpy(open_request, &OPEN_CONN_REQUEST, sizeof(OPEN_CONN_REQUEST));

    // Send the open connection request
    if (sizeof(OPEN_CONN_REQUEST) != safe_send(sock, open_request, sizeof(OPEN_CONN_REQUEST)))
    {
        std::cout << "Failed to send open connection request" << std::endl;
        info = "none";
        close(sock);
        sock = -1;
        return -1;
    }

    // Receive the open connection reply
    char open_reply[MAX_BUFFER_SIZE];
    ssize_t flag = safe_recv(sock, open_reply, sizeof(OPEN_CONN_REPLY));
    if (flag < 0)
    {
        std::cout << "Failed to receive open connection reply" << std::endl;
        info = "none";
        close(sock);
        sock = -1;
        return -1;
    }

    struct data_packet received_reply;
    memcpy(&received_reply, open_reply, sizeof(received_reply));

    if (received_reply.m_status != 1)
    {
        std::cout << "Server did not accept connection" << std::endl;
        close(sock);
        sock = -1;
        return -1;
    }
    else
    {
        std::cout << "Connection established" << std::endl;
    }
    return 1;
}

void list_files(int &sock)
{
    // Check the connection first
    if (info == "none" || !check_server_status(sock))
    {
        std::cout << "No connection" << std::endl;
        return;
    }

    char list_request[MAX_BUFFER_SIZE];
    if (sizeof(LIST_REQUEST) > MAX_BUFFER_SIZE)
    {
        std::cout << "Buffer size too small" << std::endl;
        close(sock);
        sock = -1;
        return;
    }

    // Send the list request
    memcpy(list_request, &LIST_REQUEST, sizeof(LIST_REQUEST));
    if (sizeof(LIST_REQUEST) != safe_send(sock, list_request, sizeof(LIST_REQUEST)))
    {
        std::cout << "Failed to send list request" << std::endl;
        close(sock);
        sock = -1;
        return;
    }

    char list_reply[MAX_BUFFER_SIZE];
    if (safe_recv(sock, list_reply, sizeof(list_reply)) < 0)
    {
        std::cout << "Failed to receive list reply" << std::endl;
        return;
    }
    struct data_packet received_reply;
    memcpy(&received_reply, list_reply, sizeof(LIST_REPLY));

    size_t length = to_little_endian(received_reply.m_length) - 13;
    char *file_list = new char[length + 1];
    memcpy(file_list, list_reply + sizeof(LIST_REPLY), length);
    file_list[length] = '\0';
    std::cout << file_list;
}

void change_directory(int &sock, std::string dir)
{
    // Check the connection first
    if (info == "none" || !check_server_status(sock))
    {
        std::cout << "No connection" << std::endl;
        return;
    }

    char cd_request[MAX_BUFFER_SIZE];
    if (sizeof(CD_REQUEST) + dir.size() > MAX_BUFFER_SIZE)
    {
        std::cout << "Buffer size too small" << std::endl;
        return;
    }

    struct data_packet cd_request_packet = {
        .m_protocol = PROTOCOL,
        .m_type = CD_REQUEST.m_type,
        .m_status = 0,
        .m_length = to_big_endian(13 + dir.size())};

    memcpy(cd_request, &cd_request_packet, sizeof(cd_request_packet));
    memcpy(cd_request + sizeof(cd_request_packet), dir.c_str(), dir.size());

    size_t send_bits = sizeof(cd_request_packet) + 1 + dir.size();
    cd_request[send_bits - 1] = '\0';

    // Send the change directory request
    if (send_bits != safe_send(sock, cd_request, send_bits))
    {
        std::cout << "Failed to send change directory request" << std::endl;
        return;
    }

    char cd_reply[MAX_BUFFER_SIZE];
    struct data_packet cd_reply_packet;
    if (safe_recv(sock, cd_reply, sizeof(CD_REPLY_SUCCESS)) < 0)
    {
        std::cout << "Failed to receive change directory reply" << std::endl;
        return;
    }

    // std::cout << sizeof(cd_reply) << std::endl;

    memcpy(&cd_reply_packet, cd_reply, sizeof(CD_REPLY_SUCCESS));
    if (cd_reply_packet.m_status == 0)
    {
        std::cout << "Failed to change directory" << std::endl;
        return;
    }
    else if (cd_reply_packet.m_type != CD_REPLY_SUCCESS.m_type)
    {
        std::cout << (static_cast<int>(cd_reply_packet.m_type)) << std::endl;
        std::cout << "CD_REPLY_SUCCESS has a wrong type." << std::endl;
        return;
    }
    else if (memcmp(&cd_reply_packet.m_protocol, &CD_REPLY_SUCCESS.m_protocol, MAGIC_NUMBER_LENGTH) != 0)
    {
        std::cout << "CD_REPLY_SUCCESS has a wrong protocol." << std::endl;
        return;
    }
    else
    {
        print_request(cd_reply_packet, "CD_REPLY_SUCCESS:");
        std::cout << "Directory changed" << std::endl;
    }
}

int close_connection(int &sock)
{
    // Check the connection first
    if (!check_server_status(sock))
    {
        std::cout << "No connection" << std::endl;
        return -1;
    }

    char quit_request[MAX_BUFFER_SIZE];
    if (sizeof(QUIT_REQUEST) > MAX_BUFFER_SIZE)
    {
        std::cout << "Buffer size too small" << std::endl;
        close(sock);
        sock = -1;
        return -1;
    }

    std::cout << "Closing connection" << std::endl;

    // Send the quit request
    memcpy(quit_request, &QUIT_REQUEST, sizeof(QUIT_REQUEST));

    int flag = safe_send(sock, quit_request, sizeof(QUIT_REQUEST));
    if (sizeof(QUIT_REQUEST) != flag)
    {
        std::cout << "Failed to send quit request" << std::endl;
        close(sock);
        sock = -1;
        return -1;
    }

    char quit_reply[MAX_BUFFER_SIZE];
    if (safe_recv(sock, quit_reply, sizeof(QUIT_REPLY)) < 0)
    {
        std::cout << "Failed to receive quit reply" << std::endl;
        return -1;
    }

    struct data_packet received_reply;
    memcpy(&received_reply, quit_reply, sizeof(QUIT_REPLY));
    if (received_reply.m_type != QUIT_REPLY.m_type)
    {
        std::cout << "QUIT_REPLY has a wrong type." << std::endl;
        return -1;
    }

    std::cout << "Connection closed" << std::endl;
    close(sock);
    sock = -1;
    return 1;
}

void download_files(int &sock, std::string file_name)
{
    // Check the connection first
    if (info == "none" || !check_server_status(sock))
    {
        std::cout << "No connection" << std::endl;
        return;
    }

    char get_request[MAX_BUFFER_SIZE];
    if (sizeof(GET_REQUEST) > MAX_BUFFER_SIZE)
    {
        std::cout << "Buffer size too small" << std::endl;
        return;
    }

    // Send the GET_REQUEST
    struct data_packet get_request_packet = {
        .m_protocol = PROTOCOL,
        .m_type = GET_REQUEST.m_type,
        .m_status = 0,
        .m_length = to_big_endian(13 + file_name.size())};
    memcpy(get_request, &get_request_packet, sizeof(get_request_packet));
    memcpy(get_request + sizeof(get_request_packet), file_name.c_str(), file_name.size());

    size_t send_bits = sizeof(get_request_packet) + 1 + file_name.size();
    get_request[send_bits - 1] = '\0';

    if (send_bits != safe_send(sock, get_request, send_bits))
    {
        std::cout << "Failed to send get request" << std::endl;
        return;
    }

    // Receive the GET_REPLY
    char get_reply[MAX_BUFFER_SIZE];
    if (safe_recv(sock, get_reply, sizeof(GET_REPLY_SUCCESS)) < 0)
    {
        std::cout << "Failed to receive get reply" << std::endl;
        return;
    }

    struct data_packet received_reply;
    memcpy(&received_reply, get_reply, sizeof(GET_REPLY_SUCCESS));

    if (received_reply.m_status == 0)
    {
        std::cout << "Do not find " << file_name << std::endl;
        return;
    }

    // Receive the FILE_DATA
    char file_data[MAX_BUFFER_SIZE];
    struct data_packet file_data_packet;
    if (safe_recv(sock, file_data, sizeof(file_data_packet)) < 0)
    {
        std::cout << "Failed to receive file data" << std::endl;
        return;
    }
    memcpy(&file_data_packet, file_data, sizeof(file_data_packet));
    size_t file_size = to_little_endian(file_data_packet.m_length) - 12;

    char *file_content = new char[file_size];
    if (safe_recv(sock, file_content, file_size) < 0)
    {
        std::cout << "Failed to receive file content" << std::endl;
        return;
    }

    std::cout << "Downloading file" << std::endl;

    // Find the file is exist or not
    std::filesystem::path file_path = file_name;

    if (std::filesystem::exists(file_path))
        std::cout << "File already exists: " << file_path << " Will Cover" << std::endl;

    std::ofstream file(file_path, std::ios::binary);
    if (!file)
    {
        std::cout << "Failed to create file" << std::endl;
        return;
    }
    file.write(file_content, file_size);
    if (!file)
    {
        std::cout << "Failed to write file" << std::endl;
        return;
    }
    std::cout << "File downloaded " << file_name << std::endl;
    file.close();
}

void upload_files(int &sock, std::string file_name)
{
    if (info == "none" || !check_server_status(sock))
    {
        std::cout << "No connection" << std::endl;
        return;
    }
    std::filesystem::path file_path = file_name;
    if (!std::filesystem::exists(file_path))
    {
        std::cout << "File does not exist" << std::endl;
        return;
    }

    // Send PUT_REQUEST
    char put_request[MAX_BUFFER_SIZE];
    struct data_packet put_request_packet = {
        .m_protocol = PROTOCOL,
        .m_type = PUT_REQUEST.m_type,
        .m_status = 0,
        .m_length = to_big_endian(13 + file_name.size())};
    memcpy(put_request, &put_request_packet, sizeof(put_request_packet));
    memcpy(put_request + sizeof(put_request_packet), file_name.c_str(), file_name.size());

    size_t send_bits = sizeof(put_request_packet) + 1 + file_name.size();
    put_request[send_bits - 1] = '\0';
    if (send_bits != safe_send(sock, put_request, send_bits))
    {
        std::cout << "Failed to send put request" << std::endl;
        return;
    }

    // Receive PUT_REPLY
    char put_reply[MAX_BUFFER_SIZE];
    if (safe_recv(sock, put_reply, sizeof(PUT_REPLY)) < 0)
    {
        std::cout << "Failed to receive put reply" << std::endl;
        return;
    }
    struct data_packet received_reply;
    memcpy(&received_reply, put_reply, sizeof(PUT_REPLY));
    if (PUT_REPLY.m_type != received_reply.m_type)
    {
        std::cout << "PUT_REPLY has a wrong type." << std::endl;
        return;
    }

    // Send FILE_DATA
    size_t file_size = std::filesystem::file_size(file_path);
    char *file_content = new char[file_size];
    std::ifstream file(file_path, std::ios::binary);
    if (!file)
    {
        std::cout << "Failed to open file" << std::endl;
        return;
    }
    file.read(file_content, file_size);
    if (!file)
    {
        std::cout << "Failed to read file" << std::endl;
        return;
    }
    file.close();

    // Create FILE_DATA packet
    struct data_packet file_data_packet = {
        .m_protocol = PROTOCOL,
        .m_type = FILE_DATA.m_type,
        .m_status = 0,
        .m_length = to_big_endian(12 + file_size)};
    char file_data_head[MAX_BUFFER_SIZE];
    memcpy(file_data_head, &file_data_packet, sizeof(file_data_packet));

    // Send FILE_DATA HEAD
    if (sizeof(file_data_packet) != safe_send(sock, file_data_head, sizeof(file_data_packet)))
    {
        std::cout << "Failed to send file data head" << std::endl;
        return;
    }

    // Send FILE_DATA payload
    if (file_size != safe_send(sock, file_content, file_size))
    {
        std::cout << "Failed to send file content" << std::endl;
        return;
    }
    std::cout << "File uploaded: " << file_name << std::endl;
}

void sha256(int &sock, std::string file_name)
{
    // Check the connection first
    if (info == "none" || !check_server_status(sock))
    {
        std::cout << "No connection" << std::endl;
        return;
    }

    char sha256_request[MAX_BUFFER_SIZE];

    // Send the SHA_REQUEST
    struct data_packet sha_request_packet = {
        .m_protocol = PROTOCOL,
        .m_type = SHA_REQUEST.m_type,
        .m_status = 0,
        .m_length = to_big_endian(13 + file_name.size())};
    memcpy(sha256_request, &sha_request_packet, sizeof(sha_request_packet));
    memcpy(sha256_request + sizeof(sha_request_packet), file_name.c_str(), file_name.size());

    size_t send_bits = sizeof(sha_request_packet) + 1 + file_name.size();
    sha256_request[send_bits - 1] = '\0';

    if (send_bits != safe_send(sock, sha256_request, send_bits))
    {
        std::cout << "Failed to send sha256 request" << std::endl;
        return;
    }

    // Receive the SHA_REPLY and Check the status
    char sha256_reply[MAX_BUFFER_SIZE];
    if (safe_recv(sock, sha256_reply, sizeof(SHA_REPLY_SUCCESS)) < 0)
    {
        std::cout << "Failed to receive sha256 reply" << std::endl;
        return;
    }
    struct data_packet received_reply;
    memcpy(&received_reply, sha256_reply, sizeof(SHA_REPLY_SUCCESS));
    if (received_reply.m_status == SHA_REPLY_FAIL.m_status)
    {
        std::cout << "File does not exist" << std::endl;
        return;
    }

    // Receive the FILE_DATA HEAD
    char file_data[MAX_BUFFER_SIZE];
    struct data_packet file_data_packet;
    if (safe_recv(sock, file_data, sizeof(file_data_packet)) < 0)
    {
        std::cout << "Failed to receive file data" << std::endl;
        return;
    }
    memcpy(&file_data_packet, file_data, sizeof(file_data_packet));

    // Receive the FILE_DATA payload
    ssize_t file_size = to_little_endian(file_data_packet.m_length) - 13;
    char *sha256_value = new char[file_size + 1];
    if (safe_recv(sock, sha256_value, file_size + 1) < 0)
    {
        std::cout << "Failed to receive sha256 value" << std::endl;
        return;
    }
    sha256_value[file_size] = '\0';
    std::cout << "SHA256: " << sha256_value << std::endl;
}

int main(int argc, char *argv[])
{
    std::string input;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        std::cout << "Failed to create socket" << std::endl;
        return -1;
    }

    while (true)
    {
        // Create a socket
        if (sock == -1)
        {
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0)
            {
                std::cout << "Failed to create socket" << std::endl;
                return -1;
            }
        }

        std::cout << "Client(" << info << ")> ";
        std::getline(std::cin, input);

        if (input.empty())
        {
            continue;
        }

        std::istringstream iss(input);
        std::string command;
        iss >> command;

        // 1. Open a Connection
        if (command == "open")
        {
            std::string server_ip, server_port;
            iss >> server_ip >> server_port;
            if (server_ip.empty() || server_port.empty())
            {
                std::cout << "Usage: open <server_ip> <server_port>" << std::endl;
                continue;
            }

            // Create server address
            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(std::stoi(server_port));
            inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

            // Connect to the server
            if (0 > connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)))
            {
                std::cout << "Failed to connect to server" << std::endl;
                close(sock);
                sock = -1;
                continue;
            }

            // Send the open connection request
            if (1 == open_connection(sock, server_ip, server_port))
            {
                info = server_ip + ":" + server_port;
            }
            else
                info = "none";
        }
        // 2. List files in the current directory of the server
        else if (command == "ls")
        {
            list_files(sock);
            continue;
        }
        // 3. cd DIR
        else if (command == "cd")
        {
            std::string dir;
            iss >> dir;
            if (dir.empty())
            {
                std::cout << "Usage: cd <dir>" << std::endl;
                continue;
            }
            change_directory(sock, dir);
            continue;
        }
        // 4. get FILE
        else if (command == "get")
        {
            std::string file;
            iss >> file;
            if (file.empty())
            {
                std::cout << "Usage: get <file>" << std::endl;
                continue;
            }
            download_files(sock, file);
        }
        // 5. put FILE
        else if (command == "put")
        {
            std::string file;
            iss >> file;
            if (file.empty())
            {
                std::cout << "Usage: put <file>" << std::endl;
                continue;
            }
            upload_files(sock, file);
        }
        // 6. sha256 FILE
        else if (command == "sha256")
        {
            std::string file;
            iss >> file;
            if (file.empty())
            {
                std::cout << "Usage: sha256 <file>" << std::endl;
                continue;
            }
            sha256(sock, file);
        }
        // 7. Quit
        else if (command == "quit")
        {
            if (info == "none")
            {
                std::cout << "Close Client" << std::endl;
                close(sock);
                sock = -1;
                break;
            }
            else
            {
                if (close_connection(sock))
                    info = "none";
                else
                    std::cout << "Failed to close connection" << std::endl;
            }
            continue;
        }
        else
        {
            std::cout << "Invalid command" << std::endl;
        }
    }
}
