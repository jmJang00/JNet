#pragma once

struct FContentHandle
{
	enum : unsigned long long
	{
		NONE = 0,
		INVALID_HANDLE = 0xFFFFFFFFFFFFFFFF,
	};

	union
	{
		struct
		{
			int index;
			int id;
		};
		unsigned long long handle;
	};

	FContentHandle(unsigned long long initialValue)
		: handle(initialValue)
	{
	}

	FContentHandle(int id, int idx)
		: id(id)
		, index(idx)
	{
	}
	
	bool operator==(const FContentHandle& other)
	{
		return other.handle == handle;
	}

	bool operator!=(const FContentHandle& other)
	{
		return other.handle != handle;
	}
};
