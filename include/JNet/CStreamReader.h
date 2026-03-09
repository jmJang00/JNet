#pragma once

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
