#ifndef FILE_VECTOR_HPP
#define FILE_VECTOR_HPP

#include <vector>
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
// The template stops trivial pointers and references,
// but not ones embedded in structs.

template <typename T, typename = void> class file_vector;

template <typename T>
class file_vector<T, typename enable_if<!(is_pointer<T>::value || is_reference<T>::value)>::type> {
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
        template <typename InputIterator> 
        static void forward(InputIterator first, InputIterator last, pointer dst) {
            copy(first, last, dst);
        }
        template <typename InputIterator> 
        static void backward(InputIterator first, InputIterator last, pointer dst) {
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
        template <typename... Args>
        static void single(pointer value, Args&&... args) {
            new (static_cast<void*>(value)) value_type(std::forward<Args>(args)...);
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
        template <typename InputIterator> 
        static void forward(InputIterator first, InputIterator last, pointer dst) {
            while (first < last) {
                new (static_cast<void*>(dst++)) value_type(*first++);
            }
        }
        template <typename InputIterator> 
        static void backward(InputIterator first, InputIterator last, pointer dst) {
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

        // Posix does not allow mmap of zero size.
        if (reserved > 0) {
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
            throw runtime_error("Unanble to extend memory for file_vector resize.");
        }

        // Second, map the resized file to a new address, sharing the elements.
        pointer new_values = nullptr;
        if (size > 0) {
            new_values = static_cast<pointer>(mmap(nullptr
            , size * value_size
            , PROT_READ | PROT_WRITE
            , MAP_SHARED
            , fd
            , 0
            ));

            if (new_values == nullptr) {
                throw runtime_error("Unable to mmap file for file_vector resize.");
            }
        }

        // Third, unmap the file from the old address.
        if (reserved > 0) {
            if (values != nullptr && munmap(values, reserved * value_size) == -1) {
                if (new_values != nullptr && munmap(new_values, size) == -1) {
                    throw runtime_error(
                        "Unable to munmap file while "
                        "handling failed munmap for file_vector."
                    );
                };
                throw runtime_error("Unable to munmap file for file_vector resize.");
            }
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

    void close() {
        if (values != nullptr) {
            if (munmap(values, reserved * value_size) == -1) {
                throw runtime_error("Unable to munmap file when closing file_vector.");
            }
            values = nullptr;
        }
        if (fd != -1) {
            if (ftruncate(fd, used * value_size) == -1) {
                throw runtime_error("Unable to resize file when closing file_vector.");
            }
            if (::close(fd) == -1) {
                throw runtime_error("Unable to close file when closing file_vector.");
            }
            fd = -1;
        }
        reserved = 0;
        used = 0;
    }

    virtual ~file_vector() noexcept {
        if (values != nullptr) {
            if (values != nullptr) {
                munmap(values, reserved);
                values = nullptr;
            }
            if (fd != -1) {
                ftruncate(fd, used * value_size);
                ::close(fd);
                fd = -1;
            }
            reserved = 0;
            used = 0;
        }
    }

    //------------------------------------------------------------------------
    // Vectors are provided with value identity, so vectors are equal if their
    // contents are equal, and assignment copies contents from one vector to
    // another, but does not assign the file name, so a vector file can 
    // be copied and opened like this:
    //
    // file_vector<T> dst_file("dst_file", file_vector<T>("src_file"));
    
    file_vector(string const& name)
    : name(name), reserved(0), used(0), fd(-1), values(nullptr) {
        map_file_into_memory();
    }

    file_vector(string const& name, const int n)
    : name(name), reserved(0), used(0), fd(-1), values(nullptr) {
        map_file_into_memory();
        assign(n);
    }

    file_vector(string const& name, const int n, const_reference value)
     : name(name), reserved(0), used(0), fd(-1), values(nullptr) {
        map_file_into_memory();
        assign(n, value);
    }

    template <typename InputIterator>
    file_vector(string const& name, InputIterator first, InputIterator last)
    : name(name), reserved(0), used(0), fd(-1), values(nullptr) {
        assert (first <= last);

        map_file_into_memory();
        assign(first, last);
    }

    file_vector(string const& name, file_vector const& from)
    : file_vector(name, from.cbegin(), from.cend()) {}

    file_vector(string const& name, file_vector&& from) 
    : file_vector(name, from.cbegin(), from.cend()) {}

    file_vector(string const& name, initializer_list<value_type> const& list)
    : file_vector(name, list.begin(), list.end()) {}

    file_vector(string const& name, vector<value_type> const& src)
    : file_vector(name, src.cbegin(), src.cend()) {}

    // Value equality.
    bool operator== (file_vector const& that) const {
        return (used == that.size()) && range_same(that.cbegin(), that.cend(), cbegin()); 
    }

    bool operator== (vector<value_type> const& that) const {
        return (used == that.size()) && range_same(that.cbegin(), that.cend(), cbegin());
    }

    // Copy contents.
    file_vector& operator= (file_vector const& src) {
        assign(src.cbegin(), src.cend());
        return *this;
    }

    file_vector& operator= (vector<T> const& src) {
        assign(src.cbegin(), src.cend());
        return *this;
    }

    operator vector<T>() {
        vector<T> dst;
        dst.assign(begin(), end());
        return dst;
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

    class range {
        friend file_vector;
        range(iterator const &f, iterator const &l) : first(f), last(l) {}

    public:    
        using iterator = iterator;

        iterator const first;
        iterator const last;
    };

    range all() const {
        return range(begin(), end());
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

    class reverse_range {
        friend file_vector;
        reverse_range(reverse_iterator const &f, reverse_iterator const &l) : first(f), last(l) {}

    public:    
        using iterator = reverse_iterator;

        reverse_iterator const first;
        reverse_iterator const last;
    };

    reverse_range reverse_all() const {
        return reverse_range(rbegin(), rend());
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

    class const_range {
        friend file_vector;
        const_range(const_iterator const &f, const_iterator const &l) : first(f), last(l) {}

    public:    
        using iterator = const_iterator;

        const_iterator const first;
        const_iterator const last;
    };

    const_range const_all() const {
        return const_range(cbegin(), cend());
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

    class const_reverse_range {
        friend file_vector;
        const_reverse_range(const_reverse_iterator const &f, const_reverse_iterator const &l)
            : first(f), last(l) {}

    public:    
        using iterator = const_reverse_iterator;

        const_reverse_iterator const first;
        const_reverse_iterator const last;
    };

    const_reverse_range const_reverse_all() const {
        return const_reverse_range(crbegin(), crend());
    }

    //------------------------------------------------------------------------
    
    template <typename I> bool range_same(I first, I last, const_iterator with) const {
        assert(first < last);

        while (first != last) {
            if (*first++ != *with++) {
                return false;
            }
        }
        return true;
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
        if (used + size > reserved) {
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
        assert(0 <= i && i < used);

        return values[i];
    }

    reference operator[] (int const i) {
        assert(0 <= i && i < used);

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
        assert(used > 0);

        return values[0];
    }

    reference front() {
        assert(used > 0);

        return values[0];
    }

    const_reference back() const {
        assert(used > 0);

        return values[used - 1];
    }

    reference back() {
        assert(used > 0);

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
    
    template <typename InputIterator>
    void assign(InputIterator first, InputIterator last) {
        assert(first <= last);

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
                construct<value_type>::forward(first + used, last, values + used);
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
                construct<value_type>::many(
                    values + used, values + size, value
                );
            }
        }
        
        used = size;
    }

    //------------------------------------------------------------------------

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

    //------------------------------------------------------------------------

    // Fill
    iterator insert(
        const_iterator position, size_type n, value_type const& value
    ) {
        assert(
            values <= position.values &&
            position.values <= values + used
        );

        // cannot use position after reserve, must use offset.
        difference_type const offset = position.values - values;

        if (n == 0) {
            return begin() + offset;
        } else if (n > used - offset) {
            // inserted values spill into unininitialised memory
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
            // copied values spill into uninitialised memory
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
        assert(
            values <= position.values &&
            position.values <= values + used
        );

        return insert(position, 1, value);
    }

    // Range
    template <typename I, typename = typename I::iterator_category>
    iterator insert(const_iterator position, I first, I last) {
        assert(
            values <= position.values &&
            position.values <= values + used &&
            first <= last
        );

        // cannot use position after reserve, must use offset.
        difference_type const offset = position.values - values;
        size_type const n = last - first;

        if (n <= 0) {
            return begin() + offset;
        } else if (n > used - offset) {
            // inserted values spill into unininitialised memory
            reserve(n);
            construct<value_type>::backward(
                values + offset,
                values + used,
                values + used + n
            );
            copy_n(first, used - offset, values + offset);
            construct<value_type>::forward(
                first + (used - offset),
                last,
                values + used
            );
        } else {
            // copied values spill into uninitialised memory
            reserve(n);
            construct<value_type>::backward(
                values + used - n,
                values + used,
                values + used + n
            );
            copy_backward(values + offset, values + used - n, values + used);
            copy_n(first, n, values + offset);
        }

        used += n;
        return begin() + offset;
    }

    // Initialiser List
    iterator insert(
        const_iterator position, initializer_list<value_type> list
    ) {
        assert(
            values <= position.values &&
            position.values <= values + used
        );

        return insert(position, list.begin(), list.end());
    }

    // TODO Insert Move?

    //------------------------------------------------------------------------
  
    iterator erase(const_iterator position) {
        assert(
            values <= position.values &&
            position.values < values + used
        );

        difference_type const offset = position.values - values;
         
        destroy<value_type>::single(values + offset);
        copy(values + offset + 1, values + used, values + offset);
        --used;
        return begin() + offset; 
    }

    iterator erase(const_iterator first, const_iterator last) {
        assert(
            values <= first.values &&
            first.values <= last.values &&
            last.values < values + used
        );

        difference_type const offset = first.values - values;
        size_type const n = last - first;

        destroy<value_type>::many(values + offset, values + offset + n);
        copy(values + offset + n, values + used, values + offset);
        used -= n;
        return begin() + offset;
    }

    //------------------------------------------------------------------------

    void swap(file_vector& that) {
        vector<value_type> const tmp(that);
        that = *this;
        *this = tmp;
    }

    //------------------------------------------------------------------------
    
    template <typename... Args> void emplace_back(Args&&... args) {
        reserve(1);
        construct<value_type>::single(
            values + (used++), forward<Args>(args)...
        );
    }
        
    template <typename... Args>
    iterator emplace(const_iterator position, Args&&... args) {
        assert(
            values <= position.values &&
            position.values < values + used
        );

        difference_type const offset = position.values - values;

        reserve(1);
        if (offset == used) {
            construct<value_type>::single(
                values + used, forward<Args>(args)...
            );
        } else {
            construct<value_type>::single(values + used, values[used - 1]);
            copy_backward(values + offset, values + used - 1, values + used); 
            values[offset] = value_type(forward<Args>(args)...);
        }

        ++used;
        return begin() + offset;
    }
};

template <typename T> void swap (file_vector<T>& a, file_vector<T>& b) {
    vector<T> tmp(a);
    a = b;
    b = tmp;
}

#endif
