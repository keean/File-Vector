#include <iostream>
#include <cassert>
#include "file_vector.hpp"

extern "C" {
    #include <unistd.h>
}

using namespace std;

void test_out_of_range(file_vector<int>& fv, int const i) {
    try { 
        int const tmp = fv.at(i);
    } catch (out_of_range const& e) {
        return;
    } catch (exception const& e) {
        throw runtime_error("unexpected exception.");
    }
    throw runtime_error("Did not get out-of-range exception.");
}

int main() {
    size_t const page_size = getpagesize();
    file_vector<int> vector_test1("test1");

    vector_test1.clear();

    assert(vector_test1.size() == 0);

    for (int i = 0; i < page_size; ++i) {
        vector_test1.push_back(i);    
        assert(vector_test1.at(i) == i);    
    }

    assert(vector_test1.size() == page_size);

    for (int i = 0; i < page_size; ++i) {
        assert(vector_test1.at(i) == i);    
    }

    for (
        file_vector<int>::iterator i {vector_test1.begin()};
        i != vector_test1.end();
        ++i
    ) {
        *i = 1;
        assert(*i == 1);
    }

    assert(vector_test1.size() == page_size);
     
    for (
        file_vector<int>::const_reverse_iterator i {vector_test1.crbegin()};
        i != vector_test1.crend();
        ++i
    ) {
        assert(*i == 1);
    }

    assert(vector_test1.size() == page_size);
     
    for (int i = 0; i < page_size; ++i) {
        assert(vector_test1.at(i) == 1);    
    }

    assert(vector_test1.size() == page_size);

    for (int i = 0; i < page_size; ++i) {
        vector_test1.push_back(2);   
    }

    assert(vector_test1.size() == 2 * page_size);
     
    test_out_of_range(vector_test1, 2 * page_size);

    file_vector<int> vector_test2("test2");
    vector_test2.assign(vector_test1.cbegin(), vector_test1.cend());

    vector_test1.close();
    vector_test2.close();
    cout << "Done." << endl;

    struct int_obj {
        int x;
        int_obj(int x) : x(x) {}
        ~int_obj() {x = 0;}
    };

    int_obj io {3};
    file_vector<int_obj> vector_test3("test3");
    for (int i = 0; i < page_size; ++i) {
        vector_test3.push_back(io);   
    }

    file_vector<int_obj> vector_test4("test4");
    vector_test4.assign(vector_test3.cbegin(), vector_test3.cend());
    //vector_test4 = vector_test3;

    vector_test3.close();
    vector_test4.close();
}
