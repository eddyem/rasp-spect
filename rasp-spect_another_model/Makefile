PROGRAM = websocktest
LDFLAGS = $(shell pkg-config --libs libwebsockets) -lpthread
#ifneq (,$(findstring "arm",$(shell uname -m)))
ifneq (,$(filter arm%, $(shell uname -m)))
LDFLAGS += -lwiringPi -lwiringPiDev
endif
SRCS = main.c stepper.c image.c
CC = gcc
DEFINES = -DCUR_PATH=\"$(shell pwd)\"
#DEFINES += -DEBUG
CXX = gcc
CFLAGS = -Wall -Werror -Wextra $(DEFINES) $(shell pkg-config --cflags libwebsockets)
OBJS = $(SRCS:.c=.o)
all : $(PROGRAM) clean
$(PROGRAM) : $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(PROGRAM)

# some addition dependencies
# %.o: %.c
#        $(CC) $(LDFLAGS) $(CFLAGS) $< -o $@
#$(SRCS) : %.c : %.h $(INDEPENDENT_HEADERS)
#        @touch $@

clean:
	/bin/rm -f *.o *~
depend:
	$(CXX) -MM $(CXX.SRCS)
