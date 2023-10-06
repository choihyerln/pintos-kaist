cd userprog
make clean
make
cd build
source ../../activate
# pintos -- -q run priority-donate-chain
# pintos -- -q run alarm-simultaneous
# pintos --gdb -- run priority-donate-nest

pintos --fs-disk=10 -p tests/userprog/exit:exit -- -q -f run 'exit'
# pintos --fs-disk=10 -p tests/userprog/exit --gdb -- -q -f run 'exit'