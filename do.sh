cd threads
make clean
make
cd build
source ../../activate
# pintos -- -q run priority-condvar
pintos -- -q run alarm-simultaneous
# pintos --gdb -- run priority-donate-multiple