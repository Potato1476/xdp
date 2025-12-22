#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <iomanip>
#include <vector>
#include <map>
#include <algorithm>

#define CHUNK_SIZE 972
#define HEADER_SIZE 4
#define ACK_TIMEOUT_MS 500    // Timeout 500ms
#define WINDOW_SIZE 20        // Kích thước cửa sổ

struct PacketHeader {
    uint32_t pkt_num;
};

struct AckPacket {
    uint32_t ack_num;
};

struct WindowPacket {
    std::vector<char> data;
    uint32_t pkt_num;
    std::chrono::high_resolution_clock::time_point send_time;
    bool acked;
    int retry_count;
};

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <file_path> <receiver_ip> <port>" << std::endl;
        return 1;
    }

    const char* file_path = argv[1];
    const char* receiver_ip = argv[2];
    int port = std::stoi(argv[3]);

    std::cout << "Sử dụng giao thức: Selective Repeat" << std::endl;

    // Mở file
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Không thể mở file: " << file_path << std::endl;
        return 1;
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::cout << "Kích thước file: " << file_size << " bytes (" 
                << std::fixed << std::setprecision(2) << file_size / 1024.0 / 1024.0 << " MB)" << std::endl;

    // Đọc toàn bộ file vào bộ nhớ
    std::vector<char> file_data(file_size);
    file.read(file_data.data(), file_size);
    file.close();

    // Tính số packet
    uint64_t total_packets = (file_size + CHUNK_SIZE) / CHUNK_SIZE;
    std::cout << "Tổng số packets: " << total_packets << std::endl;
    std::cout << "Window size: " << WINDOW_SIZE << std::endl;

    // Tạo UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Không thể tạo socket" << std::endl;
        return 1;
    }

    // Set socket non-blocking
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // 10ms timeout cho recvfrom
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Cấu hình địa chỉ receiver
    struct sockaddr_in receiver_addr;
    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(port);
    inet_pton(AF_INET, receiver_ip, &receiver_addr.sin_addr);

    // Sliding window
    std::map<uint32_t, WindowPacket> window;
    uint32_t base = 1;           // Packet đầu tiên chưa được ACK
    uint32_t next_seq_num = 1;   // Packet tiếp theo cần gửi

    uint64_t total_bytes_sent = 0;
    uint64_t total_retransmissions = 0;
    uint64_t acks_received = 0;

    auto start_time = std::chrono::high_resolution_clock::now();
    auto last_progress_time = start_time;

    std::cout << "Bắt đầu truyền với Selective Repeat..." << std::endl;

    while (base <= total_packets) {
        auto now = std::chrono::high_resolution_clock::now();

        // Gửi các packet mới trong window
        while (next_seq_num < base + WINDOW_SIZE && next_seq_num <= total_packets) {
            // Tạo packet
            WindowPacket pkt;
            pkt.pkt_num = next_seq_num;
            pkt.acked = false;
            pkt.retry_count = 0;

            // Tính vị trí trong file
            size_t offset = (next_seq_num - 1) * CHUNK_SIZE;
            size_t chunk_size = std::min((size_t)CHUNK_SIZE, file_data.size() - offset);

            // Tạo data với header
            pkt.data.resize(HEADER_SIZE + chunk_size);
            PacketHeader* header = (PacketHeader*)pkt.data.data();
            header->pkt_num = next_seq_num;
            memcpy(pkt.data.data() + HEADER_SIZE, file_data.data() + offset, chunk_size);

            // Gửi packet
            pkt.send_time = now;
            ssize_t sent = sendto(sock, pkt.data.data(), pkt.data.size(), 0,
                                    (struct sockaddr*)&receiver_addr, sizeof(receiver_addr));

            if (sent > 0) {
                window[next_seq_num] = pkt;
                total_bytes_sent += (sent - HEADER_SIZE);
            }
            
            next_seq_num++;
        }

        // Kiểm tra timeout và gửi lại (Selective Repeat - chỉ gửi lại packet timeout)
        for (auto& pair : window) {
            uint32_t seq = pair.first;
            WindowPacket& pkt = pair.second;
            
            if (!pkt.acked) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pkt.send_time);
                
                if (elapsed.count() >= ACK_TIMEOUT_MS) {
                    // Selective Repeat: Chỉ gửi lại packet timeout này
                    pkt.send_time = now;
                    pkt.retry_count++;
                    
                    sendto(sock, pkt.data.data(), pkt.data.size(), 0,
                          (struct sockaddr*)&receiver_addr, sizeof(receiver_addr));
                    total_retransmissions++;
                    
                    std::cout << "\nTimeout! Selective Repeat: Gửi lại packet " << seq << std::endl;
                }
            }
        }

        // Nhận ACK (Individual ACK)
        AckPacket ack;
        struct sockaddr_in ack_addr;
        socklen_t ack_addr_len = sizeof(ack_addr);
        
        ssize_t ack_recv = recvfrom(sock, &ack, sizeof(ack), 0,
                                    (struct sockaddr*)&ack_addr, &ack_addr_len);

        if (ack_recv > 0) {
            uint32_t ack_num = ack.ack_num;
            acks_received++;

            // Selective Repeat: Individual ACK - đánh dấu packet đã nhận ACK
            if (window.find(ack_num) != window.end()) {
                window[ack_num].acked = true;
            }
            
            // Di chuyển base nếu các packet liên tiếp từ base đã được ACK
            while (window.find(base) != window.end() && window[base].acked) {
                window.erase(base);
                base++;
            }
        }

        // Hiển thị tiến trình mỗi 500ms
        auto progress_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_progress_time);
        if (progress_elapsed.count() >= 500) {
            float progress = (float)(base - 1) / total_packets * 100;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
            double speed = (total_bytes_sent / 1024.0 / 1024.0) / (elapsed.count() / 1000.0);
            
            std::cout << "\rTiến trình: " << (base - 1) << "/" << total_packets 
                        << " (" << std::fixed << std::setprecision(1) << progress << "%) - "
                        << std::setprecision(2) << speed << " MB/s - "
                        << "Window: [" << base << "-" << (next_seq_num - 1) << "] - "
                        << "Retrans: " << total_retransmissions << std::flush;
            
            last_progress_time = now;
        }
    }

    // Kết thúc
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "\n\n=== KẾT QUẢ GỬI (Selective Repeat) ===" << std::endl;
    std::cout << "Tổng thời gian: " << std::fixed << std::setprecision(3) 
                << duration.count() / 1000.0 << " giây" << std::endl;
    std::cout << "Tổng số packets: " << total_packets << std::endl;
    std::cout << "ACKs nhận được: " << acks_received << std::endl;
    std::cout << "Tổng số lần truyền lại: " << total_retransmissions << std::endl;
    std::cout << "Tỷ lệ truyền lại: " << std::setprecision(2)
                << (total_packets > 0 ? (total_retransmissions * 100.0 / total_packets) : 0) << "%" << std::endl;
    std::cout << "Tổng dữ liệu đã gửi: " << std::setprecision(2) 
                << total_bytes_sent / 1024.0 / 1024.0 << " MB" << std::endl;
    std::cout << "Tốc độ trung bình: " << std::setprecision(2) 
                << (total_bytes_sent / 1024.0 / 1024.0) / (duration.count() / 1000.0) 
                << " MB/s" << std::endl;
    std::cout << "Tốc độ trung bình: " << std::setprecision(2) 
                << (total_bytes_sent * 8.0 / 1024.0 / 1024.0) / (duration.count() / 1000.0) 
                << " Mbps" << std::endl;

    close(sock);
    return 0;
}