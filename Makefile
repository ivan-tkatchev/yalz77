
all: yalz

testlz77: testlz77.cc lz77.h
	g++ -std=c++11 -Wall -O3 testlz77.cc -o testlz77

yalz: yalz.cc lz77.h
	g++ -std=c++11 -Wall -ggdb -O3 yalz.cc -o yalz


