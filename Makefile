VERSION 	= 0.1

TARGET	= gbatticon
NAME		= gBattIcon
SRC		= gbatticon.c
OBJ		= $(SRC:.c=.o)
CONFIG	= config.h

INCS		= `pkg-config --cflags gtk+-2.0`
LIBS		= `pkg-config --libs gtk+-2.0`

CC 		= gcc
LFLAGS	= -lc -lm ${LIBS}
CFLAGS	= -std=c99 -pedantic -Wall ${INCS}
CFLAGS  += -DVERSION=\"$(VERSION)\" -DTARGET=\"$(TARGET)\" -DNAME=\"$(NAME)\"

all: $(TARGET)

$(TARGET): $(OBJ)
	@echo -e "\033[0;35m LD $@\033[0m"
	@$(CC) $(CFLAGS) $(LFLAGS) -o $@ $^ 

$(OBJ): $(SRC) $(CONFIG)
	@echo -e "\033[0;32m CC $@\033[0m"
	@$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

clean:
	@echo -e "\033[0;33m RM $(TARGET) \033[0m"
	@rm -f $(TARGET)
	@echo -e "\033[0;33m RM $(OBJ) \033[0m"
	@rm -f $(OBJ)

info:
	@echo $(TARGET) build options:
	@echo "CC		= ${CC}"
	@echo "CFLAGS	= ${CFLAGS}"
	@echo "LFLAGS	= ${LFLAGS}"

.PHONY: all clean info
