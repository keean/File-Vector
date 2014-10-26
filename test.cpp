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
    file_vector<int> vector_test1("test1", page_size);

    for (int i = 0; i < page_size; ++i) {
        vector_test1[i] = i;    
        assert(vector_test1[i] == i);    
    }

    for (int i = 0; i < page_size; ++i) {
        assert(vector_test1[i] == i);    
    }

    for (
        file_vector<int>::iterator i {vector_test1.begin()};
        i != vector_test1.end();
        ++i
    ) {
        *i = 0;
        assert(*i == 0);
    }

    for (
        file_vector<int>::const_reverse_iterator i {vector_test1.crbegin()};
        i != vector_test1.crend();
        ++i
    ) {
        assert(*i == 0);
    }

    for (int i = 0; i < page_size; ++i) {
        vector_test1.push_back(page_size + i);   
    }

    test_out_of_range(vector_test1, 2 * page_size);

oore_done:

    vector_test1.close();
    cout << "Done." << endl;
}
