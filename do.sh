cd threads
make clean
make
cd build
source ../../activate
pintos -- -q run priority-donate-sema
# pintos --gdb -- run priority-donate-multiple