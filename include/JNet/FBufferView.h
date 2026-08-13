#pragma once
#include <stdint.h>

template<typename T>
struct FBufferView
{
    T* data;
    unsigned short size;

    FBufferView()
        : data(nullptr)
        , size(0)
    {
    }

    FBufferView(T* data, int size)
        : data(data)
        , size(size)
    {
    }

    const T& operator[](int index) const
    {
        return data[index];
    }

    const T* Begin() const
    {
        return data;
    }

    const T* End() const
    {
        return data + size;
    }

    const T* Data() const
    {
        return data;
    }

    uint32_t Size() const
    {
        return size;
    }

    uint32_t ByteSize() const
    {
        return sizeof(T) * size;
    }

    bool Empty() const
    {
        return size == 0;
    }
};
