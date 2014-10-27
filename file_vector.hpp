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
    size_type used;
    int fd;
    pointer values;

public:
    virtual ~file_vector() noexcept {
        if (values != nullptr) {
            munmap(values, reserved);
            ftruncate(fd, used * value_size);
            ::close(fd);
            values = nullptr;
            used = 0;
        }
    }

    void close() {
        if (values != nullptr) {
            if (munmap(values, reserved * value_size) == -1) {
                throw runtime_error("Unable to munmap file when closing file_vector.");
            }
            if (ftruncate(fd, used * value_size) == -1) {
                throw runtime_error("Unable to reused file when closing file_vector.");
            }
            if (::close(fd) == -1) {
                throw runtime_error("Unable to close file when closing file_vector.");
            }
            values = nullptr;
            used = 0;
        }
    }

    file_vector(string const& name, const_reference src) {
        fd = open(name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

        if (fd == -1) {
            throw runtime_error("Unable to open file for file_vector.");
        }
    }
        
    file_vector(string const& name, size_type size = 1) : name(name)
    , used(0)
    , reserved((size > 0) ? size : 1)
    {
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

        values = static_cast<pointer>(mmap(nullptr
        , reserved * value_size
        , PROT_READ | PROT_WRITE
        , MAP_SHARED
        , fd
        , 0
        ));

        if (values == nullptr) {
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
        pointer values;

        iterator(pointer values) : values(values)
            {}
    public:
        using difference_type = difference_type;
        using value_type = value_type;
        using reference = reference;
        using pointer = pointer;
        using iterator_category = random_access_iterator_tag;

        // All Iterators
        iterator(iterator const &that) : values(that.values)
            {}
        iterator& operator= (iterator const &that)
            {values = that.values; return *this;}
        iterator& operator++ ()
            {++values; return *this;}
        iterator operator++ (int)
            {iterator i {*this}; ++values; return i;}

        // Input
        bool operator== (iterator const &that) const
            {return values == that.values;}
        bool operator!= (iterator const &that) const
            {return values != that.values;}
        const_pointer operator-> () const
            {return values;}

        // Output
        reference operator* () const
            {return *values;}

        // Bidirectional
        iterator& operator-- () 
            {--values; return *this;}
        iterator operator-- (int)
            {iterator i {*this}; --values; return i;}

        // Random Access
        difference_type operator- (iterator const &that) const
            {return values - that.values;}
        iterator operator- (difference_type const n) const
            {return iterator(values - n);}
        iterator operator+ (difference_type const n) const
            {return iterator(values + n);}
        bool operator< (iterator const &that) const
            {return values < that.values;}
        bool operator> (iterator const &that) const
            {return values > that.values;}
        bool operator<= (iterator const &that) const   
            {return values <= that.values;}
        bool operator>= (iterator const &that) const
            {return values >= that.values;}
        iterator& operator += (difference_type const n)
            {values += n; return *this;}
        iterator& operator -= (difference_type const n)
            {values -= n; return *this;}
        reference operator[] (difference_type offset) const
            {return values[offset];}
    };

    iterator begin() {
        return iterator(values);
    }

    iterator end() {
        return iterator(values + used);
    }

    //------------------------------------------------------------------------
    // Reverse Iterator
    
    class reverse_iterator {
        friend file_vector;
        pointer values;

        reverse_iterator(pointer values) : values(values)
            {}
    public:
        using difference_type = difference_type;
        using value_type = value_type;
        using reference = reference;
        using pointer = pointer;
        using iterator_category = random_access_iterator_tag;

        // All Iterators
        reverse_iterator(reverse_iterator const &that) : values(that.values)
            {}
        reverse_iterator& operator= (reverse_iterator const &that)
            {values = that.values; return *this;}
        reverse_iterator& operator++ ()
            {--values; return *this;}
        reverse_iterator operator++ (int)
            {reverse_iterator i {*this}; --values; return i;}

        // Input
        bool operator== (reverse_iterator const &that) const
            {return values == that.values;}
        bool operator!= (reverse_iterator const &that) const
            {return values != that.values;}
        const_pointer operator-> () const
            {return values;}

        // Output
        reference operator* () const
            {return *values;}

        // Bidirectional
        reverse_iterator& operator-- ()
            {++values; return *this;}
        reverse_iterator operator-- (int)
            {reverse_iterator i {*this}; ++values; return i;}

        // Random Access
        difference_type operator- (reverse_iterator const &that)
            {return that.values - values;}
        reverse_iterator operator- (difference_type const n)
            {return reverse_iterator(values + n);}
        reverse_iterator operator+ (difference_type const n)
            {return reverse_iterator(values - n);}
        bool operator< (reverse_iterator const &that) const
            {return values > that.values;}
        bool operator> (reverse_iterator const &that) const
            {return values < that.values;}
        bool operator<= (reverse_iterator const &that) const
            {return values >= that.values;}
        bool operator>= (reverse_iterator const &that) const
            {return values <= that.values;}
        reverse_iterator& operator += (difference_type const n)
            {values -= n; return *this;}
        reverse_iterator& operator -= (difference_type const n)
            {values += n; return *this;}
        reference& operator[] (difference_type offset) const
            {return values[offset];}
    };

    reverse_iterator rbegin() {
        return reverse_iterator(values + used - 1);
    }

    reverse_iterator rend() {
        return reverse_iterator(values - 1);
    }

    //------------------------------------------------------------------------
    // Constant Iterator
    
    class const_iterator {
        friend file_vector;
        const_pointer values;

        const_iterator(const_pointer values) : values(values)
            {}
    public:
        using difference_type = difference_type;
        using value_type = value_type;
        using reference = const_reference;
        using pointer = const_pointer;
        using iterator_category = random_access_iterator_tag;

        // All Iterators
        const_iterator(const_iterator const &that) : values(that.values)
            {}
        const_iterator& operator= (const_iterator const &that)
            {values = that.values; return *this;}
        const_iterator& operator++ ()
            {++values; return *this;}
        const_iterator operator++ (int)
            {const_iterator i {*this}; ++values; return i;}

        // Input
        bool operator== (const_iterator const &that) const
            {return values == that.values;}
        bool operator!= (const_iterator const &that) const
            {return values != that.values;}
        const_pointer operator-> () const
            {return values;}
        const_reference operator* () const
            {return *values;}

        // Bidirectional
        const_iterator& operator-- () 
            {--values; return *this;}
        const_iterator operator-- (int) 
            {const_iterator i {*this}; --values; return i;}

        // Random Access
        difference_type operator- (const_iterator const &that) const
            {return values - that.values;}
        const_iterator operator- (difference_type const n) const
            {return const_iterator {values - n};}
        const_iterator operator+ (difference_type const n) const
            {return const_iterator {values + n};}
        bool operator< (const_iterator const &that) const
            {return values < that.values;}
        bool operator> (const_iterator const &that) const
            {return values > that.values;}
        bool operator<= (const_iterator const &that) const
            {return values <= that.values;}
        bool operator>= (const_iterator const &that) const
            {return values >= that.values;}
        const_iterator& operator += (difference_type const n)
            {values += n; return *this;}
        const_iterator& operator -= (difference_type const n)
            {values -= n; return *this;}
        const_reference operator[] (difference_type offset) const
            {return values[offset];}
    };

    const_iterator cbegin() {
        return const_iterator(values);
    }

    const_iterator cend() {
        return const_iterator(values + used);
    }

    //------------------------------------------------------------------------
    // Constant Reverse Iterator
    
    class const_reverse_iterator {
        friend file_vector;
        const_pointer values;

        const_reverse_iterator(const_pointer values) : values(values)
            {}
    public:
        using difference_type = difference_type;
        using value_type = value_type;
        using reference = const_reference;
        using pointer = const_pointer;
        using iterator_category = random_access_iterator_tag;

        // All Iterators
        const_reverse_iterator(const_reverse_iterator const &that) : values(that.values)
            {}
        const_reverse_iterator& operator= (const_reverse_iterator const &that)
            {values = that.values; return *this;}
        const_reverse_iterator& operator++ ()
            {--values; return *this;}
        const_reverse_iterator operator++ (int)
            {const_reverse_iterator i {*this}; --values; return i;}

        // Input
        bool operator== (const_reverse_iterator const &that) const
            {return values == that.values;}
        bool operator!= (const_reverse_iterator const &that) const
            {return values != that.values;}
        const_pointer operator-> () const
            {return values;}
        const_reference operator* () const
            {return *values;}

        // Bidirectional
        const_reverse_iterator& operator-- () 
            {++values; return *this;}
        const_reverse_iterator operator-- (int) 
            {const_reverse_iterator i {*this}; ++values; return i;}

        // Random Access
        difference_type operator- (const_reverse_iterator const &that) const
            {return that.values - values;}
        const_reverse_iterator operator- (difference_type const n) const
            {return const_reverse_iterator {values + n};}
        const_reverse_iterator operator+ (difference_type const n) const
            {return const_reverse_iterator {values - n};}
        bool operator< (const_reverse_iterator const &that) const
            {return values > that.values;}
        bool operator> (const_reverse_iterator const &that) const
            {return values < that.values;}
        bool operator<= (const_reverse_iterator const &that) const
            {return values >= that.values;}
        bool operator>= (const_reverse_iterator const &that) const
            {return values <= that.values;}
        const_reverse_iterator& operator += (difference_type const n)
            {values -= n; return *this;}
        const_reverse_iterator& operator -= (difference_type const n)
            {values += n; return *this;}
        const_reference operator[] (difference_type offset) const
            {return values[offset];}
    };

    const_reverse_iterator crbegin() {
        return const_reverse_iterator(values + used - 1);
    }

    const_reverse_iterator crend() {
        return const_reverse_iterator(values - 1);
    }

    //------------------------------------------------------------------------
    // Capacity
    
    size_type size() const {
        return used;
    }

    size_type capacity() const {
        return reserved;
    }

    void reserve(size_type const new_reserved) {
        if (new_reserved != reserved) {
            if (ftruncate(fd, new_reserved * value_size) == -1) {
                throw runtime_error("Unanble to extend memory for file_vector.");
            }

            pointer new_values = static_cast<pointer>(mmap(nullptr
            , new_reserved * value_size
            , PROT_READ | PROT_WRITE
            , MAP_SHARED
            , fd
            , 0
            ));

            if (new_values == nullptr) {
                throw runtime_error("Unable to mmap file for file_vector.");
            }

            if (munmap(values, reserved * value_size) == -1) {
                if (munmap(new_values, new_reserved) == -1) {
                    throw runtime_error(
                        "Unable to munmap file while "
                        "handling failed munmap for file_vector."
                    );
                };
                throw runtime_error("Unable to munmap file for file_vector.");
            }

            values = new_values;
            reserved = new_reserved;
        }
    }

    void resize(size_type const new_used) {
        if (new_used > reserved) {
            reserve(new_used);
        } 
        used = new_used;
    }

    bool empty() const {
        return used == 0;
    }

    void shrink_to_fit() {
        reserve(used);
    }

    //------------------------------------------------------------------------
    // Element Access

    const_reference operator[] (int const i) const {
        return values[i];
    }

    reference operator[] (int const i) {
        return values[i];
    }

    const_reference at(int const i) const {
        if (i < 0 || i >= used) {
            throw out_of_range("file_vector::at(int)");
        } 
        return values[i];
    }

    reference at(int const i) {
        if (i < 0 || i >= used) {
            throw out_of_range("file_vector::at(int)");
        } 
        return values[i];
    }

    const_reference front() const {
        return values[0];
    }

    reference front() {
        return values[0];
    }

    const_reference back() const {
        return values[used - 1];
    }

    reference back() {
        return values[used - 1];
    }

    const_pointer data() const noexcept {
        return values;
    }

    pointer data() noexcept {
        return values;
    }

    //------------------------------------------------------------------------
    // Modifiers
    
    template <typename InputIterator> void assign(InputIterator first, InputIterator last) {
        difference_type const size = last - first;
        if (size > used) {
            resize(size);
        }
        copy(first, last, begin());
    }

    // void assign(initializer_list<value_type> l) {}
    void assign(size_type const used, const_reference value) {
        resize(used);
        for (int i = 0; i < used; ++i) {
            values[i] = value;
        }
    }

    void push_back(const_reference value) {
        if (used >= reserved) {
            reserve(used + used);
        }
        values[used++] = value;
    }

    void pop_back() {
        --used;
        // need to delete
    }

    void clear() {
        used = 0;
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

