UNAME := $(shell uname)

ifeq ($(UNAME), AIX)
all : objs shared_library

objs: agent.o gcpausemonitor
shared_library : 
	xlC_r -q64 -G -o gc_pause_agent.so agent.o -lpthread 

gcpausemonitor : socket_client.c
	xlC_r -q64 socket_client.c -o gcpausemonitor -qpic

agent.o : agent.c
	xlC_r -q64 -c agent.c -I /service/Prakash/AIX/jvmap6470_27sr4-20150504_01/include 

clean:
	rm -rf gcpausemonitor agent.o gc_pause_agent.so
endif
