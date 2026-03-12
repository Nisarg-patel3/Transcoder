rm -rf ./build/*
cd build
cmake -DCMAKE_BUILD_TYPE=Debug .. 
make -j 3
cp ../input*.* ./