T = ios_instruments_client
O = obj
all: $(T)
.PHONY: all clean

CFLAGS = -g -O0 -mmacosx-version-min=10.9

ifeq ($(wildcard $(O)/.),)
  $(shell mkdir -p 2>/dev/null $(O))
endif

MAIN    = $(O)/$(T).o
COMMON  = $(O)/common.o
CFTYPES = $(O)/cftypes.o
MD      = $(O)/mobile_device.o

$(T): $(MAIN) $(COMMON) $(CFTYPES) $(MD)
	g++ $(CFLAGS) -o $@ $^ -lobjc -framework Foundation -framework CoreFoundation

$(MAIN): $(T).cpp $(T).h cftypes.h common.h mobile_device.h
	g++ $(CFLAGS) -c -o $@ $<

$(COMMON): common.cpp common.h
	g++ $(CFLAGS) -c -o $@ $<

$(CFTYPES): cftypes.cpp cftypes.h common.h
	g++ $(CFLAGS) -c -o $@ -x objective-c++ $<

$(MD): mobile_device.cpp mobile_device.h cftypes.h common.h
	g++ $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(T) $(T).dSYM $(O)/*
