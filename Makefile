CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -D_GNU_SOURCE
TARGET = project1
SIGNAL_TARGET = project1_signals
SIGNAL_TESTER = signal_tester
OBJS = project1.o
SIGNAL_OBJS = project1_signals.o

all: $(TARGET) $(SIGNAL_TARGET) $(SIGNAL_TESTER)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) -lrt

$(SIGNAL_TARGET): $(SIGNAL_OBJS)
	$(CC) $(CFLAGS) -o $(SIGNAL_TARGET) $(SIGNAL_OBJS) -lrt

project1.o: project1.c
	$(CC) $(CFLAGS) -c project1.c

project1_signals.o: project1_signals.c
	$(CC) $(CFLAGS) -c project1_signals.c

$(SIGNAL_TESTER): signal_tester.c
	$(CC) -Wall -Wextra -std=c99 -o $(SIGNAL_TESTER) signal_tester.c

clean:
	rm -f $(OBJS) $(SIGNAL_OBJS) $(TARGET) $(SIGNAL_TARGET) $(SIGNAL_TESTER)

# Test targets
test_quick: $(TARGET)
	./$(TARGET) 1000 4

test_signals: $(SIGNAL_TARGET)
	./$(SIGNAL_TARGET) 10000 4 1

# Signal testing
signal_test: $(SIGNAL_TARGET) $(SIGNAL_TESTER)
	chmod +x simple_signal_test.sh
	./simple_signal_test.sh

.PHONY: all clean test_quick test_signals signal_test