.PHONY: clean default distclean install lint uninstall

APP      := aclc
CXX      := clang++
CXXFLAGS := -std=c++17 -Wall -Werror -g
LDFLAGS  := -lfmt -lgmp -lgmpxx

SRCS := $(shell find . -name '*.cpp')
HDRS := $(shell find . -name '*.hpp')
OBJS := $(SRCS:./%.cpp=build/%.o)

default: $(APP)

$(APP): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I. -MMD -MP -c $< -o $@

-include $(OBJS:.o=.d)

lint: $(HDRS) $(SRCS)
	@cpplint --quiet --linelength=120 --filter=-legal/copyright,-runtime/references,-build/c++11 $^

clean:
	@$(RM) -r build

distclean: clean
	@$(RM) $(APP)

PREFIX = /usr/local
SHAREDIR = $(PREFIX)/share/acl

install: $(APP)
	install -d $(PREFIX)/bin $(SHAREDIR)/runtime $(SHAREDIR)/lib
	install -m 755 $(APP) $(PREFIX)/bin/$(APP)
	install -m 644 ../runtime/main.c ../runtime/runtime.c ../runtime/runtime.h $(SHAREDIR)/runtime
	install -m 644 ../lib/*.acl $(SHAREDIR)/lib

uninstall:
	$(RM) $(PREFIX)/bin/$(APP)
	$(RM) -r $(SHAREDIR)
