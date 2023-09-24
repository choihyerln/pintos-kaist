cd threads
make clean
make
cd build
source ../../activate
pintos --gdb -- -q run alarm-multiple
