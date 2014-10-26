all : test

test: test.cpp file_vector.hpp
	clang++ -ggdb -march=native -O3 -flto -std=c++11 -lrt -o test test.cpp

