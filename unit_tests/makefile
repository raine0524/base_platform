CXX = g++
INCPATH = -I../include
CFLAGS = -std=c++11 -O0 -g3 -Wall -c -fmessage-length=0

LIBPATH = -L../lib64 -L/usr/lib/python3.5/config-3.5m-x86_64-linux-gnu -lkbase -lpython3.5 -pthread
LFLAGS = -rdynamic -Wl,--unresolved-symbols=ignore-in-shared-libs -Wl,-rpath=. -Wl,-rpath=../lib64

OBJPATH = obj/
PY_PLOT_TEST = py_plot_ex
SIM_CRAWLER = simple_crawler

all : $(PY_PLOT_TEST) $(SIM_CRAWLER)

$(PY_PLOT_TEST) : $(OBJPATH)$(PY_PLOT_TEST).o
	@echo 'building target: $@'
	$(CXX) $(LFLAGS) $^ -o $@ $(LIBPATH)
	@echo ' '

$(OBJPATH)$(PY_PLOT_TEST).o : $(PY_PLOT_TEST).cpp
	@mkdir -p $(OBJPATH)
	@echo 'building file: $<'
	$(CXX) $(CFLAGS) $< -o $@ $(INCPATH)
	@echo ' '

$(SIM_CRAWLER) : $(OBJPATH)$(SIM_CRAWLER).o
	@echo 'building target: $@'
	$(CXX) $(LFLAGS) $^ -o $@ $(LIBPATH)

$(OBJPATH)$(SIM_CRAWLER).o : $(SIM_CRAWLER).cpp
	@mkdir -p $(OBJPATH)
	@echo 'building file: $<'
	$(CXX) $(CFLAGS) $< -o $@ $(INCPATH)
	@echo ' '

clean:
	rm -f $(OBJPATH)*.o
ifeq ($(PY_PLOT_TEST), $(wildcard $(PY_PLOT_TEST)))
	rm -f $(PY_PLOT_TEST)
endif
ifeq ($(SIM_CRAWLER), $(wildcard $(SIM_CRAWLER)))
	rm -f $(SIM_CRAWLER)
endif
