cd userprog
make clean
make
cd build
source ../../activate
pintos-mkdisk filesys.dsk 10 
# make check
# pintos --fs-disk=10 -p tests/userprog/write-twice:write-twice -p ../../tests/userprog/sample.txt:sample.txt -- -q -f run 'write-twice'
# pintos --fs-disk=10 -p tests/userprog/write-twice:write-twice -p ../../tests/userprog/sample.txt:sample.txt -- -q -f run write-twice
# pintos --fs-disk=10 -p tests/userprog/write-stdin:write-stdin -- -q   -f run write-stdin
# pintos --fs-disk=10 -p tests/userprog/args-many:args-many --gdb -- -q -f run 'args-many a b c d e f g h i j k l m n o p q r s t u v'
pintos --fs-disk=10 -p tests/userprog/args-many:args-many -- -q -f run 'args-many a b c d e f g h i j k l m n o p q r s t u v'
# pintos --fs-disk=10 -p tests/userprog/write-normal:write-normal -p ../../tests/userprog/sample.txt:sample.txt -- -q -f run 'write-normal'
# pintos --fs-disk=10 -p tests/userprog/read-normal:read-normal -p ../../tests/userprog/sample.txt:sample.txt -- -q   -f run read-normal
# pintos --fs-disk=10 -p tests/userprog/fork-once:fork-once -- -q -f run 'fork-once'
# pintos tests/userprog/fork-once -- -q -f run
# pintos -- -q run priority-change