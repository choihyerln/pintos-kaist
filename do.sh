cd threads
make clean
make
cd build
source ../../activate
pintos-mkdisk filesys.dsk 10 
# make check

# pintos --fs-disk=10 -p tests/userprog/fork-read:fork-read -p ../../tests/userprog/sample.txt:sample.txt -- -q -f run 'fork-read'
# pintos --fs-disk=10 -p tests/userprog/read-normal:read-normal -p ../../tests/userprog/sample.txt:sample.txt -- -q   -f run read-normal
# pintos --fs-disk=10 -p tests/userprog/fork-once:fork-once -- -q -f run 'fork-once'
# pintos tests/userprog/fork-once -- -q -f run
pintos -- -q run priority-change