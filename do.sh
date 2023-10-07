cd userprog
make clean
make
cd build
source ../../activate
# make check

pintos --fs-disk=10 -p tests/userprog/read-normal:read-normal -- -q -f run 'read-normal'
# pintos --fs-disk=10 -p tests/userprog/args-single:args-single --gdb -- -q -f run 'args-single onearg'