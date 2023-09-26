cd threads
make clean
make
cd build
source ../../activate
pintos -- -q run priority-change
# pintos -- -q run priority-donate-one
# pintos --gdb -- run priority-donate-one