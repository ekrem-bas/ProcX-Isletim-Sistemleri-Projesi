# Derleyici
CC = gcc

# Derleme Bayrakları
CFLAGS = -Wall -Wextra -g

# İşletim Sistemi Kontrolü
# Mac (Darwin) ise -lrt ekleme, Linux ise ekle.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS = -lpthread -lrt
else
    LDFLAGS = -lpthread
endif

# Hedef dosya
TARGET = procx

# Kaynak dosya
SRC = procx.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

rebuild: clean all

run: $(TARGET)
	./$(TARGET)

reset: $(TARGET)
	./$(TARGET) clean