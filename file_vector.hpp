#include <stdexcept>

extern "C" {
    #include <unistd.h>
    #include <sys/mman.h>
    #include <fcntl.h>
}

using namespace std;

template <typename T> class file_vector {
    using value_type = T;
    using reference = T&;
    using const_reference = T const&;
    using pointer = T*;
    using const_pointer = T const*;
    using difference_type = ptrdiff_t;
    using size_type = size_t;

    static size_type constexpr value_size = sizeof(T);

    string const name;
    size_type reserved;
    size_type size;
    int fd;
    pointer data;

public:
    virtual ~file_vector() noexcept {
        if (data != nullptr) {
            munmap(data, reserved);
            ftruncate(fd, size);
            ::close(fd);
            data = nullptr;
            size = 0;
        }
    }

    void close() {
        if (data != nullptr) {
            if (munmap(data, reserved) == -1) {
                throw runtime_error("Unable to munmap file when closing file_vector.");
            }
            if (ftruncate(fd, size) == -1) {
                throw runtime_error("Unable to resize file when closing file_vector.");
            }
            if (::close(fd) == -1) {
                throw runtime_error("Unable to close file when closing file_vector.");
            }
            data = nullptr;
            size = 0;
        }
    }

    file_vector(string const& name, const_reference src) {
        fd = open(name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

        if (fd == -1) {
            throw runtime_error("Unable to open file for file_vector.");
        }
    }
        
    file_vector(string const& name, size_type const size = 0) : name(name), reserved(size) , size(size) {
        fd = open(name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

        if (fd == -1) {
            throw runtime_error("Unable to open file for file_vector.");
        }

        if (ftruncate(fd, reserved * value_size) == -1) {
            if (::close(fd) == -1) {
                throw runtime_error("Unanble to close file after failing "
                    "to reserve memory for file_vector."
                );
            }
        }

        data = static_cast<pointer>(mmap(nullptr
        , reserved * value_size
        , PROT_READ | PROT_WRITE
        , MAP_SHARED
        , fd
        , 0
        ));

        if (data == nullptr) {
            if (::close(fd) == -1) {
                throw runtime_error("Unanble close file after failing "
                    "to mmap file for file_vector."
                );
            }
            throw runtime_error("Unable to mmap file for file_vector.");
        }
    }

    bool operator== (reference that) {
        return name == that.name;
    }

    reference operator= (const_reference src) {
        //close();
    }

    //------------------------------------------------------------------------
    // Iterator
    
    class iterator {
        friend file_vector;
        pointer data;

        iterator(pointer data) : data(data)
            {}
    public:
        // All Iterators
        iterator(iterator const &that) : data(that.data)
            {}
        iterator& operator= (iterator const &that)
            {data = that.data; return *this;}
        iterator& operator++ ()
            {++data; return *this;}
        iterator operator++ (int)
            {iterator i {*this}; ++data; return i;}

        // Input
        bool operator== (iterator const &that) const
            {return data == that.data;}
        bool operator!= (iterator const &that) const
            {return data != that.data;}
        const_pointer operator-> () const
            {return data;}

        // Output
        reference operator* () const
            {return *data;}

        // Bidirectional
        iterator& operator-- () 
            {--data; return *this;}
        iterator operator-- (int)
            {iterator i {*this}; --data; return i;}

        // Random Access
        difference_type operator- (iterator const &that) const
            {return data - that.data;}
        iterator operator- (difference_type const n) const
            {return iterator(data - n);}
        iterator operator+ (difference_type const n) const
            {return iterator(data + n);}
        bool operator< (iterator const &that) const
            {return data < that.data;}
        bool operator> (iterator const &that) const
            {return data > that.data;}
        bool operator<= (iterator const &that) const   
            {return data <= that.data;}
        bool operator>= (iterator const &that) const
            {return data >= that.data;}
        iterator& operator += (difference_type const n)
            {data += n; return *this;}
        iterator& operator -= (difference_type const n)
            {data -= n; return *this;}
        reference operator[] (difference_type offset) const
            {return data[offset];}
    };

    iterator begin() {
        return iterator(data);
    }

    iterator end() {
        return iterator(data + size);
    }

    //------------------------------------------------------------------------
    // Reverse Iterator
    
    class reverse_iterator {
        friend file_vector;
        pointer data;

        reverse_iterator(pointer data) : data(data)
            {}
    public:
        // All Iterators
        reverse_iterator(reverse_iterator const &that) : data(that.data)
            {}
        reverse_iterator& operator= (reverse_iterator const &that)
            {data = that.data; return *this;}
        reverse_iterator& operator++ ()
            {--data; return *this;}
        reverse_iterator operator++ (int)
            {reverse_iterator i {*this}; --data; return i;}

        // Input
        bool operator== (reverse_iterator const &that) const
            {return data == that.data;}
        bool operator!= (reverse_iterator const &that) const
            {return data != that.data;}
        const_pointer operator-> () const
            {return data;}

        // Output
        reference operator* () const
            {return *data;}

        // Bidirectional
        reverse_iterator& operator-- ()
            {++data; return *this;}
        reverse_iterator operator-- (int)
            {reverse_iterator i {*this}; ++data; return i;}

        // Random Access
        difference_type operator- (reverse_iterator const &that)
            {return that.data - data;}
        reverse_iterator operator- (difference_type const n)
            {return reverse_iterator(data + n);}
        reverse_iterator operator+ (difference_type const n)
            {return reverse_iterator(data - n);}
        bool operator< (reverse_iterator const &that) const
            {return data > that.data;}
        bool operator> (reverse_iterator const &that) const
            {return data < that.data;}
        bool operator<= (reverse_iterator const &that) const
            {return data >= that.data;}
        bool operator>= (reverse_iterator const &that) const
            {return data <= that.data;}
        reverse_iterator& operator += (difference_type const n)
            {data -= n; return *this;}
        reverse_iterator& operator -= (difference_type const n)
            {data += n; return *this;}
        reference& operator[] (difference_type offset) const
            {return data[offset];}
    };

    reverse_iterator rbegin() {
        return reverse_iterator(data + size - 1);
    }

    reverse_iterator rend() {
        return reverse_iterator(data - 1);
    }

    //------------------------------------------------------------------------
    // Constant Iterator
    
    class const_iterator {
        friend file_vector;
        const_pointer data;

        const_iterator(const_pointer data) : data(data)
            {}
    public:
        // All Iterators
        const_iterator(const_iterator const &that) : data(that.data)
            {}
        const_iterator& operator= (const_iterator const &that)
            {data = that.data; return *this;}
        const_iterator& operator++ ()
            {++data; return *this;}
        const_iterator operator++ (int)
            {const_iterator i {*this}; ++data; return i;}

        // Input
        bool operator== (const_iterator const &that) const
            {return data == that.data;}
        bool operator!= (const_iterator const &that) const
            {return data != that.data;}
        const_pointer operator-> () const
            {return data;}
        const_reference operator* () const
            {return *data;}

        // Bidirectional
        const_iterator& operator-- () 
            {--data; return *this;}
        const_iterator operator-- (int) 
            {const_iterator i {*this}; --data; return i;}

        // Random Access
        difference_type operator- (const_iterator const &that) const
            {return data - that.data;}
        const_iterator operator- (difference_type const n) const
            {return const_iterator {data - n};}
        const_iterator operator+ (difference_type const n) const
            {return const_iterator {data + n};}
        bool operator< (const_iterator const &that) const
            {return data < that.data;}
        bool operator> (const_iterator const &that) const
            {return data > that.data;}
        bool operator<= (const_iterator const &that) const
            {return data <= that.data;}
        bool operator>= (const_iterator const &that) const
            {return data >= that.data;}
        const_iterator& operator += (difference_type const n)
            {data += n; return *this;}
        const_iterator& operator -= (difference_type const n)
            {data -= n; return *this;}
        const_reference operator[] (difference_type offset) const
            {return data[offset];}
    };

    const_iterator cbegin() {
        return const_iterator(data);
    }

    const_iterator cend() {
        return const_iterator(data + size);
    }

    //------------------------------------------------------------------------
    // Constant Reverse Iterator
    
    class const_reverse_iterator {
        friend file_vector;
        const_pointer data;

        const_reverse_iterator(const_pointer data) : data(data)
            {}
    public:
        // All Iterators
        const_reverse_iterator(const_reverse_iterator const &that) : data(that.data)
            {}
        const_reverse_iterator& operator= (const_reverse_iterator const &that)
            {data = that.data; return *this;}
        const_reverse_iterator& operator++ ()
            {--data; return *this;}
        const_reverse_iterator operator++ (int)
            {const_reverse_iterator i {*this}; --data; return i;}

        // Input
        bool operator== (const_reverse_iterator const &that) const
            {return data == that.data;}
        bool operator!= (const_reverse_iterator const &that) const
            {return data != that.data;}
        const_pointer operator-> () const
            {return data;}
        const_reference operator* () const
            {return *data;}

        // Bidirectional
        const_reverse_iterator& operator-- () 
            {++data; return *this;}
        const_reverse_iterator operator-- (int) 
            {const_reverse_iterator i {*this}; ++data; return i;}

        // Random Access
        difference_type operator- (const_reverse_iterator const &that) const
            {return that.data - data;}
        const_reverse_iterator operator- (difference_type const n) const
            {return const_reverse_iterator {data + n};}
        const_reverse_iterator operator+ (difference_type const n) const
            {return const_reverse_iterator {data - n};}
        bool operator< (const_reverse_iterator const &that) const
            {return data > that.data;}
        bool operator> (const_reverse_iterator const &that) const
            {return data < that.data;}
        bool operator<= (const_reverse_iterator const &that) const
            {return data >= that.data;}
        bool operator>= (const_reverse_iterator const &that) const
            {return data <= that.data;}
        const_reverse_iterator& operator += (difference_type const n)
            {data -= n; return *this;}
        const_reverse_iterator& operator -= (difference_type const n)
            {data += n; return *this;}
        const_reference operator[] (difference_type offset) const
            {return data[offset];}
    };

    const_reverse_iterator crbegin() {
        return const_reverse_iterator(data + size - 1);
    }

    const_reverse_iterator crend() {
        return const_reverse_iterator(data - 1);
    }

    //------------------------------------------------------------------------
    // Capacity
    
    /*size_t size() const {
        return size;
    }*/

    size_type capacity() const {
        return reserved;
    }

    void reserve(size_type const new_reserved) {
        if (new_reserved != reserved) {
            if (ftruncate(fd, new_reserved * value_size) == -1) {
                throw runtime_error("Unanble to extend memory for file_vector.");
            }

            pointer new_data = static_cast<pointer>(mmap(nullptr
            , new_reserved * value_size
            , PROT_READ | PROT_WRITE
            , MAP_SHARED
            , fd
            , 0
            ));

            if (new_data == nullptr) {
                throw runtime_error("Unable to mmap file for file_vector.");
            }

            if (munmap(data, reserved) == -1) {
                if (munmap(new_data, new_reserved) == -1) {
                    throw runtime_error(
                        "Unable to munmap file while "
                        "handling failed munmap for file_vector."
                    );
                };
                throw runtime_error("Unable to munmap file for file_vector.");
            }

            data = new_data;
            reserved = new_reserved;
        }
    }

    void resize(size_type const new_size) {
        if (new_size > reserved) {
            reserve(reserved + reserved);
        } else if (new_size < reserved) {
            reserve(new_size);
        }
        size = new_size;
    }

    bool empty() const {
        return size == 0;
    }

    void shrink_to_fit() {
        reserve(size);
    }

    //------------------------------------------------------------------------
    // Element Access

    const_reference operator[] (int const i) const {
        return data[i];
    }

    reference operator[] (int const i) {
        return data[i];
    }

    const_reference at(int const i) const {
        if (i < 0 || i >= size) {
            throw out_of_range("file_vector::at(int)");
        } 
        return data[i];
    }

    reference at(int const i) {
        if (i < 0 || i >= size) {
            throw out_of_range("file_vector::at(int)");
        } 
        return data[i];
    }

    const_reference front() const {
        return data[0];
    }

    reference front() {
        return data[0];
    }

    const_reference back() const {
        return data[size - 1];
    }

    reference back() {
        return data[size - 1];
    }

    /*const_pointer data() const noexcept {
        return data;
    }

    pointer data() noexcept {
        return data;
    }*/

    //------------------------------------------------------------------------
    // Modifiers
    
    // template <typename I> void assign(I first, I last) {}
    // void assign(initializer_list<value_type> l) {}
    void assign(size_type const size, const_reference value) {
        resize(size);
        for (int i = 0; i < size; ++i) {
            data[i] = value;
        }
    }

    void push_back(const_reference value) {
        if (size >= reserved) {
            reserve(size + size);
        }
        data[size++] = value;
    }

    void pop_back() {
        --size;
        // need to delete
    }

    void clear() {
        size = 0;
        // need to delete
    }

    //------------------------------------------------------------------------
    // TODO

    // insert
    // erase
    // swap
    // emplace
    // emplace_back
};

