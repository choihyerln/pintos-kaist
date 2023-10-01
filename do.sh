cd threads
make clean
make
cd build
source ../../activate
# make check
# echo "Running Command 1"
# 1
# pintos -- -q run priority-donate-multiple
pintos -- -q run priority-donate-sema
# echo "Running Command 2"
# 2
# pintos -- -q run priority-donate-one
# pintos --gdb -- run priority-donate-multiple