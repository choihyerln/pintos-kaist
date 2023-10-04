cd threads
make clean
make
cd build
source ../../activate
pintos -- -q run priority-donate-chain
# pintos -- -q run alarm-simultaneous
# pintos --gdb -- run priority-donate-nest