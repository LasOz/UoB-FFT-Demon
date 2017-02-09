all:
#create FFT
	@g++ main.cpp `pkg-config --cflags opencv` main.cpp `pkg-config --libs opencv` -std=c++0x -pipe -Wall -Wno-switch -ggdb -g3 -O3 -ggdb -o FFT.o