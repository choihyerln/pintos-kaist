cd threads
make clean
make
cd build
source ../../activate
pintos -- -q run alarm-multiple
# pintos --gdb -q run alarm-multiple    // gdb
# 하이