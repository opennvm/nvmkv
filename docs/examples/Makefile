#Assuming that libnvm-primitives.so and libnvmkv.so are in /usr/lib/nvm
#Assuming that libvsl.so is in /usr/lib/fio
#Assuming that public header files are in /usr/include/nvm
#Assuming that device is /dev/fioa
LDFLAGS= -L/usr/lib/nvm -L/usr/lib/fio -lvsl -lnvmkv -lnvm-primitives -ldl -lpthread

CPPFLAGS=-std=gnu99

all: nvm_kv_sample
	sudo LD_LIBRARY_PATH=/usr/lib/nvm ./nvm_kv_sample "/dev/fioa"

nvm_kv_sample: nvm_kv_sample.o
	$(CC) -o $@ $< $(LDFLAGS)

clean:
	rm -f *.o nvm_kv_sample
