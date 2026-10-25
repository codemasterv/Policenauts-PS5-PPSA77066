PS5_HOST ?= ps5
PS5_PORT ?= 9021
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
ELF := mousebridge.elf
CFLAGS := -Wall -Werror -O2
all: $(ELF)
$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^ -lSceNotification
clean:
	rm -f $(ELF)
