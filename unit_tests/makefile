CXX = g++
INCPATH = -I../include
CFLAGS = -std=c++11 -O0 -g3 -Wall -c -fmessage-length=0

LIBPATH = -L../lib64 -L/usr/lib/python3.5/config-3.5m-x86_64-linux-gnu -lkbase -lpython3.5 -pthread
LFLAGS = -rdynamic -Wl,--unresolved-symbols=ignore-in-shared-libs -Wl,-rpath=. -Wl,-rpath=../lib64

OBJPATH = obj/
PY_PLOT_TEST = py_plot_ex

all : $(PY_PLOT_TEST)
py_plot : $(PY_PLOT_TEST)

$(PY_PLOT_TEST) : $(OBJPATH)$(PY_PLOT_TEST).o
	@echo 'building target: $@'
	$(CXX) $(LFLAGS) $^ -o $@ $(LIBPATH)

$(OBJPATH)$(PY_PLOT_TEST).o : $(PY_PLOT_TEST).cpp
	@mkdir -p $(OBJPATH)
	@echo 'building file: $<'
	$(CXX) $(CFLAGS) $< -o $@ $(INCPATH)
	@echo ' '

clean:
	rm -f $(OBJPATH)*.o
ifeq ($(PY_PLOT_TEST), $(wildcard $(PY_PLOT_TEST)))
	rm -f $(PY_PLOT_TEST)
endif
