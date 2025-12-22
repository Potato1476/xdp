#include <iostream>
#include <fstream>
#include <cstdint>
#include <iomanip>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <original_file> <received_file>" << std::endl;
        return 1;
    }

    const char* original_file = argv[1];
    const char* received_file = argv[2];

    std::ifstream orig(original_file, std::ios::binary);
    std::ifstream recv(received_file, std::ios::binary);

    if (!orig.is_open()) {
        std::cerr << "Không thể mở file gốc: " << original_file << std::endl;
        return 1;
    }
    if (!recv.is_open()) {
        std::cerr << "Không thể mở file nhận được: " << received_file << std::endl;
        return 1;
    }

    std::cout << "Đang so sánh byte-by-byte..." << std::endl;

    uint64_t bytes_compared = 0;
    uint64_t bytes_different = 0;

    char orig_byte = 0;
    char recv_byte = 0;

    while (true) {
        bool orig_ok = orig.read(&orig_byte, 1).good();
        bool recv_ok = recv.read(&recv_byte, 1).good();

        if (!orig_ok || !recv_ok) {
            break; // một trong hai file đã hết
        }

        if (orig_byte != recv_byte) {
            bytes_different++;
        }

        bytes_compared++;

        if (bytes_compared % 10000000 == 0) { // mỗi ~10MB
            std::cout << "\rĐã so sánh: "
                      << std::fixed << std::setprecision(2)
                      << bytes_compared / 1024.0 / 1024.0
                      << " MB" << std::flush;
        }
    }

    // Kiểm tra file dài/ngắn hơn
    orig.seekg(0, std::ios::end);
    recv.seekg(0, std::ios::end);

    uint64_t orig_size = orig.tellg();
    uint64_t recv_size = recv.tellg();

    orig.close();
    recv.close();

    std::cout << "\n\n=== KẾT QUẢ SO SÁNH ===" << std::endl;
    std::cout << "File gốc     : " << orig_size << " bytes" << std::endl;
    std::cout << "File nhận    : " << recv_size << " bytes" << std::endl;
    std::cout << "Bytes so sánh: " << bytes_compared << std::endl;
    std::cout << "Bytes khác   : " << bytes_different << std::endl;

    if (orig_size != recv_size) {
        std::cout << "⚠️  Cảnh báo: Kích thước file KHÔNG giống nhau!" << std::endl;
    }

    if (bytes_different == 0 && orig_size == recv_size) {
        std::cout << "✅ Hai file giống hệt nhau (byte-by-byte)" << std::endl;
    } else {
        double error_rate = (bytes_compared > 0)
            ? (bytes_different * 100.0 / bytes_compared)
            : 0.0;
        std::cout << "Tỷ lệ lỗi: " << std::fixed << std::setprecision(6)
                  << error_rate << " %" << std::endl;
    }

    return 0;
}