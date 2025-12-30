# Kịch Bản Test Firewall - Mô Phỏng Mất Mát Gói Tin

## 📋 Tổng Quan

Kịch bản này được thiết kế để test giao thức XDP (kết hợp TCP và UDP) trong điều kiện mất mát gói tin trên đường truyền. Firewall hoạt động như một router trung gian giữa sender và receiver, tự động phát hiện traffic và áp dụng packet loss để mô phỏng điều kiện mạng không ổn định.

## 🏗️ Kiến Trúc Hệ Thống

### Topology Mạng

```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│   Sender    │         │   Firewall   │         │  Receiver   │
│             │         │   (Router)   │         │             │
│ 172.22.0.100│────────▶│ 172.22.0.2   │────────▶│ 172.23.0.101│
│             │  netA   │              │  netB   │             │
└─────────────┘         └──────────────┘         └─────────────┘
```

### Các Container Docker

1. **Sender Container** (netA: 172.22.0.100)
   - Chứa các chương trình sender (TCP, UDP, XDP)
   - Gửi file video đến receiver qua firewall

2. **Firewall Container** (netA: 172.22.0.2, netB: 172.23.0.2)
   - Đóng vai trò router giữa 2 mạng
   - Tự động phát hiện traffic và áp dụng packet loss
   - Sử dụng `tc` (traffic control) với netem để mô phỏng mất mát

3. **Receiver Container** (netB: 172.23.0.101)
   - Nhận và lưu file video
   - So sánh với file gốc để đánh giá chất lượng

## ⚙️ Cơ Chế Hoạt Động Của Firewall

### 1. Khởi Tạo (Initialization)

Khi firewall container khởi động:

1. **Bật IP Forwarding**
   ```bash
   sysctl -w net.ipv4.ip_forward=1
   ```
   - Cho phép firewall chuyển tiếp gói tin giữa 2 mạng

2. **Thiết Lập iptables Rules**
   ```bash
   iptables -A FORWARD -i eth0 -o eth1 -j ACCEPT  # netA → netB
   iptables -A FORWARD -i eth1 -o eth0 -j ACCEPT  # netB → netA
   ```
   - Cho phép forward traffic giữa 2 interface
   - Cho phép các kết nối đã thiết lập

3. **Trạng Thái Ban Đầu**
   - **Packet loss: TẮT** (normal forwarding)
   - Firewall ở trạng thái "thụ động", không chặn gói tin
   - Cho phép sender và receiver kết nối bình thường

### 2. Giám Sát Traffic (Traffic Monitoring)

Firewall chạy một vòng lặp giám sát liên tục:

**Tần Suất Kiểm Tra**: Mỗi **100ms** (CHECK_INTERVAL = 0.1s) - Đã điều chỉnh cho file lớn (1GB)

**Cách Thức Giám Sát**:
- Đọc số lượng packet từ `/sys/class/net/eth0/statistics/` và `/sys/class/net/eth1/statistics/`
- Tính tổng số packet (TX + RX) trên cả 2 interface
- So sánh với số packet ở lần kiểm tra trước

```bash
packet_delta = current_packet_count - last_packet_count
```

### 3. Phát Hiện Traffic và Bật Packet Loss

**Điều Kiện Kích Hoạt**:
- Khi `packet_delta > 0` (có gói tin mới)

**Hành Động**:
1. **Reset idle counter** về 0
2. **Bật packet loss** nếu chưa bật:
   ```bash
   tc qdisc add dev eth0 root netem loss 10%
   tc qdisc add dev eth1 root netem loss 10%
   ```
   - Áp dụng packet loss **10%** (mặc định) trên cả 2 interface
   - Sử dụng `tc netem` để mô phỏng mất mát gói tin

**Kết Quả**:
- 🔴 Firewall chuyển sang trạng thái "ACTIVE"
- Các gói tin đi qua firewall có **10% khả năng bị drop**
- Giao thức XDP phải xử lý việc mất mát gói tin

### 4. Phát Hiện Không Có Traffic và Tắt Packet Loss

**Điều Kiện**:
- Khi `packet_delta = 0` (không có gói tin mới)
- Firewall đang ở trạng thái ACTIVE (packet loss đã bật)

**Logic Đếm Idle**:
- Tăng `idle_checks` lên 1 mỗi lần kiểm tra
- Tính `max_idle_checks = IDLE_TIMEOUT / CHECK_INTERVAL`
  - Mặc định: `3.0s / 0.1s = 30` lần kiểm tra
  - Tương đương **3 giây** không có traffic (đã điều chỉnh cho file lớn)

**Hành Động Khi Đạt Ngưỡng**:
```bash
tc qdisc del dev eth0 root
tc qdisc del dev eth1 root
```
- 🟢 Tắt packet loss
- Firewall trở về trạng thái "PASSIVE"
- Traffic được forward bình thường

## 📊 Timeline Mô Phỏng

### Kịch Bản Gửi Video Lớn (1GB)

```
Time (s)     Firewall State    Traffic        Action
─────────────────────────────────────────────────────
0.0          PASSIVE           None           Sender bắt đầu kết nối
0.1          ACTIVE            Detected       🔴 Bật packet loss 5%
0.2          ACTIVE            Streaming      Gói tin bị drop ngẫu nhiên
0.3          ACTIVE            Streaming      XDP xử lý retransmission
...          ACTIVE            Streaming      Tiếp tục streaming (có thể mất vài phút)
T            ACTIVE            Streaming      Video gần hoàn thành
T+0.1        ACTIVE            Streaming      Gửi xong
T+0.2        ACTIVE            Idle (1)       Đếm idle checks
T+0.3        ACTIVE            Idle (2)       Đếm idle checks
...          ACTIVE            Idle           Đếm idle checks
T+3.0        PASSIVE           Idle (30)      🟢 Tắt packet loss
```

### Chi Tiết Hoạt Động

1. **T=0s**: Sender bắt đầu gửi gói tin
   - Firewall phát hiện traffic trong vòng 100ms đầu tiên
   - Tự động bật packet loss 5%

2. **T=0.1s - T**: Giai đoạn streaming (có thể kéo dài vài phút với file 1GB)
   - Mỗi gói tin có 5% khả năng bị drop
   - Giao thức XDP phải:
     - Phát hiện gói tin bị mất
     - Retransmit gói tin bị mất
     - Đảm bảo tính tin cậy như TCP
   - Firewall tiếp tục giám sát traffic mỗi 100ms
   - Với IDLE_TIMEOUT=3s, firewall sẽ không tắt packet loss khi có khoảng trống nhỏ giữa các gói tin

3. **T**: Video gửi xong
   - Không còn traffic mới
   - Firewall bắt đầu đếm idle checks

4. **T+0.1s - T+3.0s**: Giai đoạn idle
   - Mỗi 100ms kiểm tra một lần
   - Sau 30 lần kiểm tra (3 giây) không có traffic
   - Tự động tắt packet loss

## 🎯 Mục Đích Test

### 1. Test Tính Tin Cậy
- Giao thức XDP phải đảm bảo tất cả gói tin được nhận đúng
- Xử lý retransmission khi gói tin bị mất
- So sánh file nhận được với file gốc

### 2. Test Hiệu Suất
- Đo thời gian truyền với packet loss
- So sánh với TCP (tin cậy nhưng chậm) và UDP (nhanh nhưng không tin cậy)
- Đánh giá sự cân bằng giữa tốc độ và độ tin cậy

### 3. Test Khả Năng Phục Hồi
- Xử lý mất mát gói tin ngẫu nhiên
- Duy trì chất lượng video
- Không bị gián đoạn kết nối

## 🔧 Cấu Hình

### Biến Môi Trường

| Biến | Mặc Định | Mô Tả |
|------|----------|-------|
| `PACKET_LOSS_RATE` | 5% | Tỷ lệ mất mát gói tin (0-100%) - Đã giảm cho file lớn |
| `CHECK_INTERVAL` | 0.1s (100ms) | Tần suất kiểm tra traffic - Đã tăng cho file lớn |
| `IDLE_TIMEOUT` | 3.0s (3000ms) | Thời gian chờ trước khi tắt packet loss - Đã tăng cho file lớn |

### Ví Dụ Cấu Hình

**Packet loss cao (20%)**:
```bash
PACKET_LOSS_RATE=20 docker-compose up firewall
```

**Phản ứng nhanh hơn (50ms check, cho file nhỏ)**:
```bash
CHECK_INTERVAL=0.05 IDLE_TIMEOUT=0.5 docker-compose up firewall
```

**Packet loss thấp (5%)**:
```bash
PACKET_LOSS_RATE=5 docker-compose up firewall
```

## 📝 Quy Trình Test

### Bước 1: Khởi Động Hệ Thống
```bash
docker-compose up -d
```

### Bước 2: Compile Các Chương Trình
```bash
# Trong container sender/receiver
g++ -o sender_xdp sender/sender_xdp.cpp
g++ -o receiver_xdp receiver/receiver_xdp.cpp
```

### Bước 3: Chạy Receiver (Chờ kết nối)
```bash
# Trong container receiver
./receiver_xdp 9999 xdp_video.mp4 video.mp4
```

### Bước 4: Chạy Sender (Bắt đầu gửi)
```bash
# Trong container sender
./sender_xdp video.mp4 172.23.0.101 9999
```

### Bước 5: Quan Sát Firewall
```bash
# Xem logs của firewall
docker logs -f firewall
```

**Output mẫu**:
```
[2024-12-30 10:13:45] 🔴 ENABLING packet loss (10%)...
[2024-12-30 10:13:45] 🔴 ENABLING packet loss (10%)...
[2024-12-30 10:13:45] 🟢 DISABLING packet loss (normal forwarding)...
```

### Bước 6: So Sánh Kết Quả
```bash
# Trong container receiver
g++ -o compare compare.cpp
./compare video.mp4 xdp_video.mp4
```

## 🎓 Kết Luận

Kịch bản này mô phỏng một cách **thực tế** điều kiện mạng không ổn định:

✅ **Tự động phát hiện traffic** - Không cần can thiệp thủ công  
✅ **Phản ứng phù hợp** - 100ms để phát hiện và bật packet loss (đã điều chỉnh cho file lớn)  
✅ **Mô phỏng chính xác** - Sử dụng `tc netem` để drop gói tin thực sự  
✅ **Linh hoạt** - Có thể điều chỉnh tỷ lệ mất mát và thời gian phản ứng  
✅ **Không ảnh hưởng kết nối ban đầu** - Cho phép handshake và setup bình thường  

Điều này cho phép đánh giá chính xác hiệu suất của giao thức XDP trong điều kiện mạng thực tế với mất mát gói tin.

