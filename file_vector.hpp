#include <stdexcept>
#include <type_traits>
#include <algorithm>

extern "C" {
    #include <unistd.h>
    #include <stdio.h>
    #include <sys/mman.h>
    #include <fcntl.h>
}

using namespace std;

// Use should be limited to data that does not contain pointers.
// Unfortunately there is no way to express this constraint using
// type_traits that could enforce this rule.

template <typename T> class file_vector {
    using value_type = T;
    using reference = T&;
    using const_reference = T const&;
    using pointer = T*;
    using const_pointer = T const*;
    using difference_type = ptrdiff_t;
    using size_type = size_t;

    //------------------------------------------------------------------------
    // Specialised private contructors and destructors for values
    
    template<typename U, typename E = void> struct construct;

    template<typename U>
    struct construct<U, typename enable_if<!is_class<U>::value || is_pod<U>::value>::type> {
        static void single(pointer value) {}
        static void single(pointer value, const_reference from) {
            *value = from;
        }
        static void many(pointer first, pointer last) {}
        static void many(pointer first, pointer last, const_reference from) {
            while (first < last) {
                *first++ = from;
            }
        }
        static void forward(pointer first, pointer last, pointer dst) {
            copy(first, last, dst);
        }
        static void backward(pointer first, pointer last, pointer dst) {
            copy_backward(first, last, dst);
        }
    };

    template<typename U>
    struct construct<U, typename enable_if<is_class<U>::value && !is_pod<U>::value>::type> {
        static void single(pointer value) {
            new (static_cast<void*>(value)) value_type();
        }
        static void single(pointer value, const_reference from) {
            new (static_cast<void*>(value)) value_type(from);
        }
        static void many(pointer first, pointer last) {
            while (first != last) {
                new (static_cast<void*>(first++)) value_type();
            }
        }
        static void many(pointer first, pointer last, const_reference from) {
            while (first < last) {
                new (static_cast<void*>(first++)) value_type(from);
            }
        }
        static void forward(pointer first, pointer last, pointer dst) {
            while (first < last) {
                new (static_cast<void*>(dst++)) value_type(*first++);
            }
        }
        static void backward(pointer first, pointer last, pointer dst) {
            while (first < last) {
                new (static_cast<void*>(--dst)) value_type(*(--last));
            }
        }
    };
    
    template<typename U, typename E = void> struct destroy;

    template<typename U>
    struct destroy<U, typename enable_if<!is_class<U>::value || is_pod<U>::value>::type> {
        static void single(pointer value) {}
        static void many(pointer first, pointer last) {}
    };

    template<typename U>
    struct destroy<U, typename enable_if<is_class<U>::value && !is_pod<U>::value>::type> {
        static void single(pointer value) {
            value->~value_type();
        }
        static void many(pointer first, pointer last) {
            while (first < last) {
                first++->~value_type();
            }
        }
    };

    //------------------------------------------------------------------------
    
    static size_type constexpr value_size = sizeof(T);

    string const name;
    size_type reserved;
    size_type used;
    int fd;
    pointer values;

    void map_file_into_memory() {
        fd = open(name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

        if (fd == -1) {
            throw runtime_error("Unable to open file for file_vector.");
        }

        size_type size = lseek(fd, 0, SEEK_END);

        if (size == -1) {
            if (::close(fd) == -1) {
                throw runtime_error("Unanble to close file after failing "
                    "to get length of file for file_vector."
                );
            }
            throw runtime_error("Unanble to get length of file for file_vector.");
        }

        used = size / value_size;
        reserved = size / value_size;

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

    //------------------------------------------------------------------------
    // Resizes the file with ftruncate, and then maps the file into
    // memory at a new address. Because we are using shared mappings, this can
    // re-use the page-cache already in memory. Finally it unmaps the old
    // mapping leaving just the new 'resized' one, and points the class to the
    // new mapping.
    
    void resize_and_remap_file(size_type const size) {
        if (size == reserved) {
            return;
        }

        // First, resize the file.
        if (ftruncate(fd, size * value_size) == -1) {
            throw runtime_error("Unanble to extend memory for file_vector.");
        }

        // Second, map the resized file to a new address, sharing the elements.
        pointer new_values = static_cast<pointer>(mmap(nullptr
        , size * value_size
        , PROT_READ | PROT_WRITE
        , MAP_SHARED
        , fd
        , 0
        ));

        if (new_values == nullptr) {
            throw runtime_error("Unable to mmap file for file_vector.");
        }

        // Third, unmap the file from the old address.
        if (munmap(values, reserved * value_size) == -1) {
            if (munmap(new_values, size) == -1) {
                throw runtime_error(
                    "Unable to munmap file while "
                    "handling failed munmap for file_vector."
                );
            };
            throw runtime_error("Unable to munmap file for file_vector.");
        }

        // Finally, update the class.
        values = new_values;
        reserved = size;
    }

    size_type grow_to(size_type const size) {
        size_type capacity = reserved * 1.5;
        if (capacity < size) {
            capacity = size;
        }
        return capacity;
    }

public:

    //------------------------------------------------------------------------
    // Vectors are provided with value identity, so vectors are equal if their
    // contents are equal, and assignment copies contents from one vector to
    // another, but does not assign the file name, so a vector file can 
    // be copied and opened like this:
    //
    // file_vector<T> dst_file("dst_file", file_vector<T>("src_file"));
    
    file_vector(string const& name) : name(name) {
        map_file_into_memory();
    }

    file_vector(string const& name, const int n) : name(name) {
        map_file_into_memory();
        assign(n);
    }

    file_vector(string const& name, const int n, const_reference value) : name(name) {
        map_file_into_memory();
        assign(n, value);
    }

    file_vector(string const& name, file_vector const& from) : name(name) {
        map_file_into_memory();
        assign(from.cbegin(), from.cend());
    }

    file_vector(string const& name, file_vector&& from) : name(name) {
        map_file_into_memory();
        assign(from.cbegin(), from.cend());
    }

    file_vector(string const& name, initializer_list<value_type> const& list) : name(name) {
        map_file_into_memory();
        assign(list);
    }

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

    // Value equality.
    bool operator== (file_vector const& that) const {
        if (used != that.size()) {
            return false;
        }
        const_iterator x = cbegin();
        const_iterator y = that.cbegin();
        for (int i = 0; i < used; ++i) {
            if (*x++ != *y++) {
                return false;
            }
        }
        return true;
    }

    // Copy contents.
    file_vector& operator= (file_vector const& src) {
        assign(src.cbegin(), src.cend());
        return *this;
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

    iterator begin() const {
        return iterator(values);
    }

    iterator end() const {
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

    reverse_iterator rbegin() const {
        return reverse_iterator(values + used - 1);
    }

    reverse_iterator rend() const {
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

    const_iterator cbegin() const {
        return const_iterator(values);
    }

    const_iterator cend() const {
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

    const_reverse_iterator crbegin() const {
        return const_reverse_iterator(values + used - 1);
    }

    const_reverse_iterator crend() const {
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

    // Resize so that the capacity is at least 'size', using a doubling 
    // algorithm. 
    void reserve(size_type const size) {
        if (size > reserved - used) {
            resize_and_remap_file(grow_to(used + size));
        }
    }

    // Default construct or destroy values as necessary 
    void resize(size_type const size) {
        if (size < used) {
            destroy<value_type>::many(values + size, values + used);
        } else if (size > used) {
            reserve(size);
            construct<value_type>::many(values + used, values + size);
        }

        used = size;
    }

    // Copy construct or destroy values as necessary
    void resize(size_type const size, const_reference value) {
        if (size < used) {
            destroy<value_type>::many(values + size, values + used);
        } else if (size > used) {
            reserve(size - used);
            construct<value_type>::many(values + used, values + size, value);
        }

        used = size;
    }
        
    bool empty() const {
        return used == 0;
    }

    void shrink_to_fit() {
        resize_and_remap_file(used);
    }

    //------------------------------------------------------------------------
    // Element Access

    // Unchecked access
    const_reference operator[] (int const i) const {
        return values[i];
    }

    reference operator[] (int const i) {
        return values[i];
    }

    // Bounds checked access
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

    // Unchecked front and back
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

    // Pointer to raw data.
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

        if (size < used) {
            // destroy any excess and copy
            destroy<value_type>::many(values + size, values + used);
            copy(first, first + size, begin());
        } else {
            // copy and construct extra
            copy(first, first + used, begin());

            if (size > used) {
                reserve(size - used);
                const_iterator src = first + used;
                pointer dst = values + used;
                while (src != last) {
                    construct<value_type>::single(dst++, *src++);
                }
            }
        }

        used = size;
    }

    void assign(initializer_list<value_type> const& list) {
        assign(list.begin(), list.end());
    }

    void assign(size_type const size) {
        value_type const tmp;

        if (size < used) {
            // destroy any excess and fill with default
            destroy<value_type>::many(values + size, values + used);
            fill(begin(), begin() + size, tmp);
        } else {
            // fill with default and construct extra
            fill(begin(), begin() + used, tmp);

            if (size > used) {
                reserve(size - used);
                construct<value_type>::many(values + used, values + size);
            }
        }

        used = size;
    }

    void assign(size_type const size, const_reference value) {
        if (size < used) {
            // destroy any excess and fill with value
            destroy<value_type>::many(values + size, values + used);
            fill(begin(), begin() + size, value);
        } else {
            // fill with value and construct extra
            fill(begin(), begin() + used, value);

            if (size > used) {
                reserve(size - used);
                construct<value_type>::many(values + used, values + size, value);
            }
        }
        
        used = size;
    }

    void push_back(const_reference value) {
        reserve(1);
        construct<value_type>::single(values + (used++), value);
    }

    void pop_back() {
        destroy<value_type>::single(values + (used--));
    }

    void clear() {
        destroy<value_type>::many(values, values + used);
        used = 0;
    }

    // Fill
    iterator insert(const_iterator position, size_type n, const value_type& value) {

        // cannot use position after reserve, must use offset.
        difference_type const offset = position.values - values;

        if (n == 0) {
            return begin() + offset;
        } else if (n > used - offset) {
            reserve(n);
            construct<value_type>::backward(
                values + offset,
                values + used,
                values + used + n
            );
            fill_n(values + offset, used - offset, value);
            construct<value_type>::many(
                values + used,
                values + offset + n,
                value
            );
        } else {
            reserve(n);
            construct<value_type>::backward(
                values + used - n,
                values + used,
                values + used + n
            );
            copy_backward(values + offset, values + used - n, values + used);
            fill_n(values + offset, n, value);
        }

        used += n;
        return begin() + offset;
    }

    // Single element 
    iterator insert(const_iterator position, value_type const& value) {
        return insert(position, 1, value);
    }

    /*
    // Range
    template <typename InputIterator>
    iterator insert(const_iterator position, InputIterator first, InputIterator last) {
        difference_type const size = last - first;
        reserve(size);
        iterator dst = begin() + (position - cbegin());
        copy_backward(dst, end(), end() + size);
        copy(first, last, dst);
        used += size;
        return dst;
    }

    // Initialiser List
    iterator insert(const_iterator position, initializer_list<value_type> list) {
        return insert(position, list.begin(), list.end());
    }
    */

    // TODO
    // erase
    // swap
    // emplace
    // emplace_back
};

