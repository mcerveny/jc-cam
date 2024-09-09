CFLAGS+=-D__TIMESTAMP_ISO__=$(shell date -u +'"\"%Y-%m-%dT%H:%M:%SZ\""')
CFLAGS+=-DVERSION='"$(shell git describe --tags)"'

CFLAGS += -DCAMPORT=8554 -DCAMAUTH='"${CAMAUTH}"'

TARGET = jc-cam

OBJS :=	main.o 

#CFLAGS += -Wall -g -O0 
CFLAGS += -Wall -O2 

INCLUDES += -I$(shell pwd)
CFLAGS += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
LDFLAGS := -lpthread -lutil -lrt -lavformat -lavcodec  -lavutil

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ -Wl,--whole-archive $(OBJS) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

%.o: %.c
	@rm -f $@ 
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@ -Wno-deprecated-declarations

%.o: %.cpp
	@rm -f $@ 
	$(CXX) $(CFLAGS) $(INCLUDES) -c $< -o $@ -Wno-deprecated-declarations

%.a: $(OBJS)
	$(AR) r $@ $^

clean:
	for i in $(OBJS) $(TARGET); do (if test -e "$$i"; then ( rm $$i ); fi ); done

