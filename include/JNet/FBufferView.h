#pragma once

#include <stdint.h>
#include <JNet/CPacket.h>
#include <JNet/CPacketView.h>

template<typename T>
struct FBufferView
{
    const T* data;
    int size;

    FBufferView()
        : data(nullptr)
        , size(0)
    {
    }

    FBufferView(const T* data, int size)
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

template <typename T>
CPacket& operator<<(CPacket& pkt, FBufferView<T>& test)
{
    pkt << test.size;
	pkt.PutData((char*)test.Begin(), test.ByteSize());
    return pkt;
}

template <typename T>
CPacketView& operator>>(CPacketView& pkt, FBufferView<T>& test)
{
    pkt >> test.size;
    test.data = pkt.GetBufferPtr() + pkt.GetReadPos();
    return pkt;
}
