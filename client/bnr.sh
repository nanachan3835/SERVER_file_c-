mkdir -p ./build
rm -rf ./build/*
cd ./build
cmake ..
make
cd ..
./build/main