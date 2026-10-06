# PS5-UZDOOM - builds uzdoom-kbd.elf, the keyboard helper payload.
#   make PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
# Copyright 2026 PS5-UZDOOM port contributors
# SPDX-License-Identifier: GPL-3.0-or-later
ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := uzdoom-kbd.elf
CFLAGS := -Wall -Werror -O2 -lSceKeyboard -lSceUserService

all: $(ELF)

$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(ELF)
