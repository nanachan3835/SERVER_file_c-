#!/bin/bash

# Dòng này rất quan trọng: Script sẽ thoát ngay lập tức nếu có bất kỳ lệnh nào thất bại.
# Điều này ngăn chặn việc thực thi các lệnh nguy hiểm (như rm) nếu một bước trước đó bị lỗi.
set -e

# 1. Tạo thư mục 'build' nếu nó chưa tồn tại.
#    Cờ '-p' đảm bảo lệnh không báo lỗi nếu thư mục đã tồn tại.
mkdir -p build

# 2. Đi vào thư mục 'build'. Lệnh này sẽ luôn thành công vì chúng ta vừa tạo nó.
cd build

# 3. Chạy CMake để tạo cấu hình build.
#    Đảm bảo đường dẫn đến vcpkg.cmake là chính xác.
cmake .. -DCMAKE_TOOLCHAIN_FILE=/home/nanachanuwu/VCPKG/vcpkg/scripts/buildsystems/vcpkg.cmake

# 4. Biên dịch dự án bằng tất cả các nhân CPU có sẵn.
make -j$(nproc)

# 5. Đi vào thư mục 'run' (nằm ở thư mục gốc của dự án, tức là '../run' so với 'build').
cd ../run

# 6. Chạy file server.
echo "--- Starting File Server ---"
./file_server