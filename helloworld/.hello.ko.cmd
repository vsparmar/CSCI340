cmd_/home/user/helloworld/hello.ko := ld -r -m elf_x86_64 -T ./scripts/module-common.lds --build-id  -o /home/user/helloworld/hello.ko /home/user/helloworld/hello.o /home/user/helloworld/hello.mod.o
