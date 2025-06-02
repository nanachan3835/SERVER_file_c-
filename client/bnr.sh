mkdir -p ./build
rm -rf ./build/*
rm -rf .cache/clangd/
cd ./build
cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make
cd ..
./build/SyncClient