cd userprog
make clean
make
cd build
source ../../activate
# make check

pintos --fs-disk=10 -p tests/userprog/open-empty:open-empty -- -q -f run 'open-empty'
# pintos --fs-disk=10 -p tests/userprog/args-single:args-single --gdb -- -q -f run 'args-single onearg'
