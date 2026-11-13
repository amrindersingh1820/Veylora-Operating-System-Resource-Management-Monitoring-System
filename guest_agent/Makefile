# Virtual OS Simulator – Guest Monitoring Agent Makefile
# Target: Linux ARM64 (and portable POSIX)

CC       ?= gcc
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra -Iinclude
TARGET   := vos_guest_agent

SRC := src/main.c \
       src/cpu_monitor.c \
       src/memory_monitor.c \
       src/disk_monitor.c \
       src/network_monitor.c \
       src/process_monitor.c \
       src/metrics_common.c \
       src/agent_protocol.c \
       src/cgroup_manager.c \
       src/app_launcher.c

OBJ := $(SRC:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

