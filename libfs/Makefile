# Target library
lib := libfs.a
targets := fs disk
objs := fs.o disk.o
CC := gcc
CFLAGS := -Wextra -MMD #-Wall
CFLAGS += -g
STATICLIB := ar rcs

ifneq ($(v), 1)
Q = @
endif

all: $(lib) $(objs)
deps := $(patsubst %.o, %.d, $(objs))
-include $(deps)

$(lib): $(objs)
	@echo "CC $@"
	$(Q) $(STATICLIB) $@ $^

$(targets): $(objs)
	@echo "CC $@"
	$(Q) $(CC) $(CFLAGS) -o $@ $<

%.o : %.c 
	@echo "CC $@"
	$(Q) $(CC) $(CFLAGS) -c -o $@ $<

clean:
	@echo "CLEAN"
	$(Q) rm -f $(lib) $(objs) $(targets) $(deps)
