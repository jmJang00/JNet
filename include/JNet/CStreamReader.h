#pragma once
#include <JNet/FBufferView.h>
#include <type_traits>
#include <string_view>

template <typename T>
class CStreamReader
{
public:
	enum ErrCode : int
	{
		Default,
		ErrorSerialize,
		ErrorDeserialize,
		ErrorResize,
	};

	CStreamReader(char* buffer, unsigned short readPos, unsigned short writePos)
		: _buffer(buffer)
		, _headPos(readPos)
		, _endPos(writePos)
		, _err(0)
	{
	}

	int GetLastError() { return _err; }
	void SetLastError(int error) { _err = error; }

	T& operator>>(char& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((char*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(unsigned char& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((unsigned char*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(bool& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((bool*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(short& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((short*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(unsigned short& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((unsigned short*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(int& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((int*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(unsigned int& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((unsigned int*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(long long& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((long long*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(unsigned long long& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((unsigned long long*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(float& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((float*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(double& value)
	{
		if (_headPos + sizeof(value) <= _endPos)
		{
			value = *((double*)(_buffer + _headPos));
			_headPos += sizeof(value);
		}
		else
		{
			_err = ErrorDeserialize;
		}
		return *(T*)this;
	}

	T& operator>>(std::wstring& value)
	{
		unsigned short size;
		*this >> size;
		if (_err != Default)
		{
			return *(T*)this;
		}

		int bytes = (int)(size * sizeof(char));
		if (_headPos + bytes > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		value.assign((wchar_t*)(_buffer + _headPos), size);
		_headPos += bytes;

		return *(T*)this;
	}

	T& operator>>(std::string& value)
	{
		unsigned short size;
		*this >> size;
		if (_err != Default)
		{
			return *(T*)this;
		}

		int bytes = (int)(size * sizeof(char));
		if (_headPos + bytes > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		value.assign((char*)(_buffer + _headPos), size);
		_headPos += bytes;

		return *(T*)this;
	}

	template <typename U>
	T& operator>>(FBufferView<U>& test);

	T& operator>>(FBufferView<wchar_t>& test)
	{
		*this >> test.size;

		if (_err != Default)
			return *(T*)this;

		int bytes = test.size;
		test.size /= 2;

		if (_headPos + bytes <= _endPos)
		{
			test.data = (wchar_t*)(_buffer + _headPos);
			_headPos += bytes;
		}
		else
		{
			_err = ErrorDeserialize;
		}

		return *(T*)this;
	}

	T& operator>>(std::wstring_view& test)
	{
		unsigned short bytes = 0;
		*this >> bytes;

		if (_err != Default)
			return *(T*)this;

		if (_headPos + bytes <= _endPos)
		{
			test = std::wstring_view((wchar_t*)(_buffer + _headPos), bytes / 2);
			_headPos += bytes;
		}
		else
		{
			_err = ErrorDeserialize;
		}

		return *(T*)this;
	}

	int GetData(char* dest, int size)
	{
		if (_headPos + size > _endPos)
		{
			_err = ErrorDeserialize;
			return 0;
		}

		memcpy(dest, _buffer + _headPos, size);
		_headPos += size;
		return size;
	}

protected:
	char* _buffer;
	unsigned short _headPos;
	unsigned short _endPos;
	int _err;
};

template <typename T>
template <typename U>
T& CStreamReader<T>::operator>>(FBufferView<U>& test)
{
	static_assert(std::is_fundamental_v<U>, "U must be a fundamental type.");

	*this >> test.size;
	if (_err != Default)
	{
		return *(T*)this;
	}

	int bytes = sizeof(U) * test.size;
	if (_headPos + bytes <= _endPos)
	{
		test.data = (U*)(_buffer + _headPos);
		_headPos += bytes;
	}
	else
	{
		_err = ErrorDeserialize;
	}

	return *(T*)this;
}

