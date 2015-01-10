all : test

test: test.cpp file_vector.hpp
	clang++ -ggdb -march=native -O3 -flto -std=c++11 -lrt -o test test.cpp

clean:
	rm -f test test1 test2 test3 test4 test5 test6 test7 test8
