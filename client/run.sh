cd build
rm -rf *
cmake .. -DCMAKE_TOOLCHAIN_FILE=/home/nanachanuwu/VCPKG/vcpkg/scripts/buildsystems/vcpkg.cmake
make -j$(nproc)
./SyncClient