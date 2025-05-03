CC=gcc
CFLAGS=`pkg-config --cflags gtk+-3.0`
LIBS=`pkg-config --libs gtk+-3.0`
TARGET=widget_breakout

all:
	$(CC) main.c -o $(TARGET) $(CFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)
