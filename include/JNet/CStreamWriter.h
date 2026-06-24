#pragma once

template <typename T>
class CStreamWriter
{
public:
	enum ErrCode : int
	{
		Default,
		ErrorSerialize,
		ErrorDeserialize,
		ErrorResize,
	};

	CStreamWriter(char* buffer, unsigned short head, unsigned short end)
		: _buffer(buffer)
		, _headPos(head)
		, _endPos(end)
		, _err(0)
	{
	}

	T& operator<<(unsigned char value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((unsigned char*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(char value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((char*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(bool value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((bool*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(short value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((short*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(unsigned short value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((unsigned short*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(unsigned int value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((unsigned int*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(int value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((int*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(long long value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((long long*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(unsigned long long value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((unsigned long long*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(float value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((float*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(double value)
	{
		if (_headPos + sizeof(value) > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*((double*)(_buffer + _headPos)) = value;
		_headPos += sizeof(value);

		return *(T*)this;
	}

	T& operator<<(std::wstring& value)
	{
		int size = (int)value.size();
		if (size > USHRT_MAX)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*this << (short)size;
		if (_err != Default)
		{
			return *(T*)this;
		}

		int bytes = (int)(size * sizeof(wchar_t));
		if (_headPos + bytes > _endPos)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		memcpy(_buffer + _headPos, value.c_str(), bytes);
		_headPos += bytes;

		return *(T*)this;
	}

	T& operator<<(std::string& value)
	{
		int size = (int)value.size();
		if (size > USHRT_MAX)
		{
			_err = ErrorSerialize;
			return *(T*)this;
		}

		*this << size;
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

		memcpy(_buffer + _headPos, value.c_str(), bytes);
		_headPos += bytes;

		return *(T*)this;
	}

	int PutData(const char* src, int size)
	{
		if (_headPos + size > _endPos)
		{
			_err = ErrorSerialize;
			return 0;
		}

		memcpy(_buffer + _headPos, src, size);
		_headPos += size;
		return size;
	}

protected:
	char* _buffer;
	unsigned short _headPos;
	unsigned short _endPos;
	int _err;
};
