#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <iomanip>

#define CHUNK_SIZE 972
#define TIMEOUT_SEC 3

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

    // Tạo UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Không thể tạo socket" << std::endl;
        return 1;
    }

    // Set timeout
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Không thể bind socket" << std::endl;
        close(sock);
        return 1;
    }

    std::cout << "Đang lắng nghe trên port " << port << "..." << std::endl;

    // Mở file để ghi
    std::ofstream file(output_file, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Không thể tạo file output: " << output_file << std::endl;
        close(sock);
        return 1;
    }

    char buffer[CHUNK_SIZE];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    uint64_t packets_received = 0;
    uint64_t total_bytes_received = 0;
    bool started = false;

    auto start_time = std::chrono::high_resolution_clock::now();
    auto last_packet_time = start_time;

    std::cout << "Đang nhận dữ liệu (UDP)..." << std::endl;

    while (true) {
        ssize_t recv_len = recvfrom(sock, buffer, sizeof(buffer), 0,
                                    (struct sockaddr*)&sender_addr, &addr_len);

        if (recv_len < 0) {
            // Timeout - kiểm tra đã nhận gì chưa
            auto now = std::chrono::high_resolution_clock::now();
            auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(now - last_packet_time);
            
            if (idle_time.count() >= TIMEOUT_SEC) {
                if (started) {
                    std::cout << "\nTimeout - kết thúc nhận dữ liệu" << std::endl;
                    break;
                }
            }
            continue;
        }

        if (!started) {
            started = true;
            start_time = std::chrono::high_resolution_clock::now();
            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
            std::cout << "Bắt đầu nhận từ: " << sender_ip << ":" << ntohs(sender_addr.sin_port) << std::endl;
        }

        last_packet_time = std::chrono::high_resolution_clock::now();

        // Ghi dữ liệu trực tiếp vào file (theo thứ tự nhận được)
        file.write(buffer, recv_len);
        
        packets_received++;
        total_bytes_received += recv_len;

        // Hiển thị tiến trình
        if (packets_received % 100 == 0) {
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - start_time);
            double speed = (total_bytes_received / 1024.0 / 1024.0) / (elapsed.count() / 1000.0);
            
            std::cout << "\rĐã nhận: " << packets_received << " packets, "
                     << std::fixed << std::setprecision(2) 
                     << total_bytes_received / 1024.0 / 1024.0 << " MB - "
                     << speed << " MB/s" << std::flush;
        }
    }

    // Kết thúc đo thời gian
    auto end_time = last_packet_time;
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    file.close();

    // Lấy kích thước file thực tế đã nhận
    std::ifstream check_file(output_file, std::ios::binary | std::ios::ate);
    std::streamsize received_size = check_file.tellg();
    check_file.close();

    // Tính toán mất mát
    int64_t data_lost = original_size - received_size;
    double loss_rate = (original_size > 0) ? (data_lost * 100.0 / original_size) : 0;

    std::cout << "\n\n=== KẾT QUẢ NHẬN (UDP) ===" << std::endl;
    std::cout << "Tổng thời gian: " << std::fixed << std::setprecision(3) 
              << duration.count() / 1000.0 << " giây" << std::endl;
    std::cout << "Packets đã nhận: " << packets_received << std::endl;
    std::cout << "Tổng dữ liệu đã nhận: " << std::setprecision(2) 
              << total_bytes_received / 1024.0 / 1024.0 << " MB" << std::endl;
    std::cout << "File gốc: " << std::setprecision(2) 
              << original_size / 1024.0 / 1024.0 << " MB" << std::endl;
    std::cout << "File nhận được: " << std::setprecision(2) 
              << received_size / 1024.0 / 1024.0 << " MB" << std::endl;
    std::cout << "Dữ liệu bị mất: " << std::setprecision(2) 
              << data_lost / 1024.0 / 1024.0 << " MB" << std::endl;
    std::cout << "Tỷ lệ mất dữ liệu: " << std::setprecision(4) 
              << loss_rate << "%" << std::endl;
    std::cout << "Tốc độ trung bình: " << std::setprecision(2) 
              << (total_bytes_received / 1024.0 / 1024.0) / (duration.count() / 1000.0) 
              << " MB/s" << std::endl;
    std::cout << "Tốc độ trung bình: " << std::setprecision(2) 
              << (total_bytes_received * 8.0 / 1024.0 / 1024.0) / (duration.count() / 1000.0) 
              << " Mbps" << std::endl;
    std::cout << "Lưu ý: UDP không đảm bảo tính toàn vẹn, không đảm bảo thứ tự và không đảm bảo tất cả gói tin đến đích. Việc tính dữ liệu bị mất chỉ dựa trên việc so sánh kích thước file nhận được với file gốc."
              << std::endl;

    close(sock);

    return 0;
}