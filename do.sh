cd userprog
make clean
make
cd build
source ../../activate
# make check

pintos --fs-disk=10 -p tests/userprog/open-bad-ptr:open-bad-ptr -- -q -f run 'open-bad-ptr'
# pintos --fs-disk=10 -p tests/userprog/args-single:args-single --gdb -- -q -f run 'args-single onearg'
