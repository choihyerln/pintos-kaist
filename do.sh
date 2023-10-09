cd userprog
make clean
make
cd build
source ../../activate
pintos-mkdisk filesys.dsk 10 
# make check

pintos --fs-disk=10 -p tests/userprog/fork-once:fork-once -- -q -f run 'fork-once'
# pintos --fs-disk=10 -p tests/userprog/fork-once:fork-once --gdb -- -q -f run 'fork-once'
