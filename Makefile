CC ?= gcc
CFLAGS ?= -O2 -Wall -Wno-unused-function -Wno-unused-variable -Wno-stringop-truncation -Wno-format-truncation -Wno-misleading-indentation
LDFLAGS ?= -lm

SRCS = src/mnos_val.c src/mnos_env.c src/mnos_lexer.c src/mnos_parser.c src/mnos_runtime.c src/mnos_mnos.c src/mnos_main.c src/mnos_tool.c
OBJS = $(SRCS:.c=.o)
TARGET = manios

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c src/mnos.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/manios
	ln -sf /usr/local/bin/manios /usr/local/bin/mno
	@echo "Installed manios to /usr/local/bin/"

.PHONY: all clean install
