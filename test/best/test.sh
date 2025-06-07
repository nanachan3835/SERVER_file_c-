#!/bin/bash

# --- BIẾN CẦN THAY ĐỔI ---
# Đảm bảo TOKEN này là token hợp lệ bạn đã lấy được từ API login của server
TOKEN="token_UID1_USERdung_UUIDf28a7d16-15f2-406a-b55f-15ff8f7d196c"

# ĐƯỜNG DẪN TUYỆT ĐỐI đến file cục bộ bạn muốn upload
# Ví dụ: /home/nanachanuwu/my_test_files/upload_me.txt
# Hãy tạo file này trước khi chạy script!
LOCAL_FILE_TO_UPLOAD="/home/nanachanuwu/CODE_LEARN_CODE_BLOCK/C++/operating_system_dropbox/test/best/main.cpp" # THAY ĐỔI ĐƯỜNG DẪN NÀY

# Đường dẫn tương đối mà file sẽ được lưu trên server (so với home của user)
SERVER_RELATIVE_UPLOAD_PATH="/home/nanachanuwu/CODE_LEARN_CODE_BLOCK/C++/operating_system_dropbox/server/data/users/main.cpp"

# URL của server API
SERVER_API_URL="http://localhost:8080/api/v1/files/upload"
# -------------------------

# Tạo file test nếu chưa có (chỉ để script chạy được)
if [ ! -f "$LOCAL_FILE_TO_UPLOAD" ]; then
  echo "Creating dummy file for upload: $LOCAL_FILE_TO_UPLOAD"
  echo "This is a test file from script." > "$LOCAL_FILE_TO_UPLOAD"
fi

echo "--- Test 1: Gửi relativePath qua Header ---"
# Đảm bảo lệnh curl nằm trên một dòng hoặc dùng \ để nối dòng
curl -X POST \
  -H "X-Auth-Token: $TOKEN" \
  -H "X-File-Relative-Path: $SERVER_RELATIVE_UPLOAD_PATH" \
  -F "file=@$LOCAL_FILE_TO_UPLOAD;filename=script_upload1.txt;type=text/plain" \
  "$SERVER_API_URL" -v

echo -e "\n\n--- Test 2: Gửi relativePath qua Form Field ---"
curl -X POST \
  -H "X-Auth-Token: $TOKEN" \
  -F "relativePath=$SERVER_RELATIVE_UPLOAD_PATH" \
  -F "file=@$LOCAL_FILE_TO_UPLOAD;filename=script_upload2.txt;type=text/plain" \
  "$SERVER_API_URL" -v

echo -e "\n\n--- Test 3: Gửi relativePath qua cả Header và Form Field ---"
curl -X POST \
  -H "X-Auth-Token: $TOKEN" \
  -H "X-File-Relative-Path: ${SERVER_RELATIVE_UPLOAD_PATH}_header" \ # Thêm _header để phân biệt
  -F "relativePath=${SERVER_RELATIVE_UPLOAD_PATH}_form" \ # Thêm _form để phân biệt
  -F "file=@$LOCAL_FILE_TO_UPLOAD;filename=script_upload3.txt;type=text/plain" \
  "$SERVER_API_URL" -v