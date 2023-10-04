cd userprog
make clean
make
cd build
source ../../activate
<<<<<<< HEAD
# make check
# echo "Running Command 1"
# 1
# pintos -- -q run priority-donate-multiple
pintos -- -q run args-single
# echo "Running Command 2"
# 2
# pintos -- -q run priority-donate-one
# pintos --gdb -- run priority-donate-multiple
=======
pintos -- -q run priority-donate-chain
# pintos -- -q run alarm-simultaneous
# pintos --gdb -- run priority-donate-nest
>>>>>>> 83635127cb61a0e26a56cac5b772c4d5b3501948
