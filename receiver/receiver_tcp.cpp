#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <iomanip>

#define CHUNK_SIZE 972

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <port> <output_file> <original_file>" << std::endl;
        return 1;
    }

    int port = std::stoi(argv[1]);
    const char* output_file = argv[2];
    const char* original_file = argv[3];

    // Kiểm tra file gốc
    std::ifstream orig_file(original_file, std::ios::binary | std::ios::ate);
    if (!orig_file.is_open()) {
        std::cerr << "Không thể mở file gốc: " << original_file << std::endl;
        return 1;
    }
    
    std::streamsize original_size = orig_file.tellg();
    orig_file.close();
    
    std::cout << "Kích thước file gốc: " << std::fixed << std::setprecision(2) 
                << original_size / 1024.0 / 1024.0 << " MB" << std::endl;

    // Tạo TCP socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        std::cerr << "Không thể tạo socket" << std::endl;
        return 1;
    }

    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Không thể bind socket trên port " << port << std::endl;
        close(server_sock);
        return 1;
    }

    // Listen
    if (listen(server_sock, 1) < 0) {
        std::cerr << "Không thể listen" << std::endl;
        close(server_sock);
        return 1;
    }

    std::cout << "Đang lắng nghe trên port " << port << "..." << std::endl;

    // Accept connection
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
    
    if (client_sock < 0) {
        std::cerr << "Không thể accept connection" << std::endl;
        close(server_sock);
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::cout << "Đã kết nối từ: " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

    // Mở file để ghi
    std::ofstream file(output_file, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Không thể tạo file output: " << output_file << std::endl;
        close(client_sock);
        close(server_sock);
        return 1;
    }

    // Bắt đầu đo thời gian
    auto start_time = std::chrono::high_resolution_clock::now();

    char buffer[CHUNK_SIZE];
    uint64_t total_received = 0;
    uint64_t chunks_received = 0;

    std::cout << "Đang nhận dữ liệu..." << std::endl;

    while (total_received < original_size) {
        // Tính số bytes cần nhận
        size_t bytes_to_receive = std::min((uint64_t)CHUNK_SIZE, original_size - total_received);
        
        // Nhận dữ liệu
        ssize_t received = recv(client_sock, buffer, bytes_to_receive, 0);
        
        if (received < 0) {
            std::cerr << "\nLỗi nhận dữ liệu" << std::endl;
            break;
        } else if (received == 0) {
            std::cerr << "\nKết nối bị đóng bởi sender" << std::endl;
            break;
        }

        // Ghi vào file
        file.write(buffer, received);
        total_received += received;
        chunks_received++;

        // Hiển thị tiến trình
        if (chunks_received % 100 == 0 || total_received >= original_size) {
            float progress = (float)total_received / original_size * 100;
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - start_time);
            double speed = (total_received / 1024.0 / 1024.0) / (elapsed.count() / 1000.0);
            
            std::cout << "\rĐã nhận: " << std::fixed << std::setprecision(2) 
                     << total_received / 1024.0 / 1024.0 << " MB / " 
                     << original_size / 1024.0 / 1024.0 << " MB"
                     << " (" << std::setprecision(1) << progress << "%) - "
                     << std::setprecision(2) << speed << " MB/s" << std::flush;
        }
    }

    // Kết thúc đo thời gian
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    file.close();

    // Lấy kích thước file thực tế đã nhận
    std::ifstream check_file(output_file, std::ios::binary | std::ios::ate);
    std::streamsize received_size = check_file.tellg();
    check_file.close();

    // Tính toán mất mát
    int64_t data_lost = original_size - received_size;
    double loss_rate = (original_size > 0) ? (data_lost * 100.0 / original_size) : 0;

    std::cout << "\n\n=== KẾT QUẢ NHẬN (TCP) ===" << std::endl;
    std::cout << "Tổng thời gian: " << std::fixed << std::setprecision(3) 
              << duration.count() / 1000.0 << " giây" << std::endl;
    std::cout << "Dữ liệu đã nhận: " << std::setprecision(2) 
              << total_received / 1024.0 / 1024.0 << " MB" << std::endl;
    std::cout << "Số chunks đã nhận: " << chunks_received << std::endl;
    
    std::cout << "File gốc: " << std::setprecision(2) 
              << original_size / 1024.0 / 1024.0 << " MB" << std::endl;
    std::cout << "File nhận được: " << std::setprecision(2) 
              << received_size / 1024.0 / 1024.0 << " MB" << std::endl;
    std::cout << "Dữ liệu bị mất: " << std::setprecision(2) 
              << data_lost / 1024.0 / 1024.0 << " MB" << std::endl;
    std::cout << "Tỷ lệ mất dữ liệu: " << std::setprecision(4) 
              << loss_rate << "%" << std::endl;
    
    std::cout << "Tốc độ trung bình: " << std::setprecision(2) 
              << (total_received / 1024.0 / 1024.0) / (duration.count() / 1000.0) 
              << " MB/s" << std::endl;
    std::cout << "Tốc độ trung bình: " << std::setprecision(2) 
              << (total_received * 8.0 / 1024.0 / 1024.0) / (duration.count() / 1000.0) 
              << " Mbps" << std::endl;

    close(client_sock);
    close(server_sock);

    return 0;
}