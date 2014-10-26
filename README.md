File-Vector
===========

C++ file backed vector for very fast column databases, useful for time series data. The API is identical to the STL vector class, except a filename is provided to the constructor, which is created if necessary and mmaped to allow fast random access. Because the destructor can fail, one additional method (close) is provied which enables exceptions during closure to be caught. There is full write support including push_back, and file-space is reserved using the usual vector doubling algorithm.
