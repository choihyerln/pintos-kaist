cd userprog
make clean
make
cd build
source ../../activate
# make check

pintos --fs-disk=10 -p tests/userprog/close-twice:close-twice -- -q -f run 'close-twice'
# pintos --fs-disk=10 -p tests/userprog/read-normal:read-normal -p ../../tests/userprog/sample.txt:sample.txt -- -q   -f run read-normal
# pintos --fs-disk=10 -p tests/userprog/args-single:args-single --gdb -- -q -f run 'args-single onearg'