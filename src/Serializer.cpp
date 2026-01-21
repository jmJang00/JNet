#include "pch.h"
#include <stdlib.h>
#include <memory.h>
#include <JNet/Serializer.h>

CTlsMemoryPool<Serializer> Serializer::_pool(PoolRegistry::RegisterDebugSign("Serializer"), true);
CRWLock Serializer::_poolLock;

Serializer& Serializer::operator=(const Serializer& clSrcPacket)
{
	_packetBuffer->Release();
	_packetBuffer = clSrcPacket._packetBuffer;
	_packetBuffer->AddRef();
	_writePos = clSrcPacket._writePos;
	_readPos = clSrcPacket._readPos;
	return *this;
}

void Serializer::Clear()
{
	_err = Default;
	_writePos = 0;
	_readPos = 0;
}


//////////////////////////////////////////////////////////////////////////
// 넣기.
//////////////////////////////////////////////////////////////////////////
Serializer& Serializer::operator<<(unsigned char value)
{
	if (_writePos + sizeof(value) > sizeof(_packetBuffer->_buffer))
	{
		//TODO: 직렬화 버퍼 개수 카운트 일치 체크
		//TODO: 리사이즈 잘 작동하는지 테스트
		_err = ErrorSerialize;
		return *this;
	}

	*((unsigned char*)(_packetBuffer->_buffer + _writePos)) = value;
	_writePos += sizeof(value);

	return *this;
}

Serializer& Serializer::operator<<(char value)
{
	if (_writePos + sizeof(value) > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	*((char*)(_packetBuffer->_buffer + _writePos)) = value;
	_writePos += sizeof(value);

	return *this;
}

Serializer& Serializer::operator<<(short value)
{
	if (_writePos + sizeof(value) > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	*((short*)(_packetBuffer->_buffer + _writePos)) = value;
	_writePos += sizeof(value);

	return *this;
}

Serializer& Serializer::operator<<(unsigned short value)
{
	if (_writePos + sizeof(value) > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	*((unsigned short*)(_packetBuffer->_buffer + _writePos)) = value;
	_writePos += sizeof(value);

	return *this;
}

Serializer& Serializer::operator<<(unsigned int value)
{
	if (_writePos + sizeof(value) > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	*((unsigned int*)(_packetBuffer->_buffer + _writePos)) = value;
	_writePos += sizeof(value);

	return *this;
}

Serializer& Serializer::operator<<(int value)
{
	if (_writePos + sizeof(value) > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	*((int*)(_packetBuffer->_buffer + _writePos)) = value;
	_writePos += sizeof(value);

	return *this;
}

Serializer& Serializer::operator<<(long long value)
{
	if (_writePos + sizeof(value) > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	*((long long*)(_packetBuffer->_buffer + _writePos)) = value;
	_writePos += sizeof(value);

	return *this;
}

Serializer& Serializer::operator<<(unsigned long long value)
{
	if (_writePos + sizeof(value) > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	*((unsigned long long*)(_packetBuffer->_buffer + _writePos)) = value;
	_writePos += sizeof(value);

	return *this;
}

Serializer& Serializer::operator<<(float value)
{
	if (_writePos + sizeof(value) > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	*((float*)(_packetBuffer->_buffer + _writePos)) = value;
	_writePos += sizeof(value);

	return *this;
}

Serializer& Serializer::operator<<(double value)
{
	if (_writePos + sizeof(value) > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	*((double*)(_packetBuffer->_buffer + _writePos)) = value;
	_writePos += sizeof(value);

	return *this;
}

Serializer& Serializer::operator<<(std::wstring& value)
{
	int size = (int)value.size();
	if (size > USHRT_MAX)
	{
		_err = ErrorDeserialize;
		return *this;
	}

	*this << (short)size;
	if (_err != Default)
	{
		return *this;
	}
	
	int bytes = (int)(size * sizeof(wchar_t));
	if (_writePos + bytes > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	memcpy(_packetBuffer->_buffer + _writePos, value.c_str(), bytes);
	_writePos += bytes;

	return *this;
}

Serializer& Serializer::operator<<(std::string& value)
{
	int size = (int)value.size();
	if (size > USHRT_MAX)
	{
		_err = ErrorDeserialize;
		return *this;
	}

	*this << size;
	if (_err != Default)
	{
		return *this;
	}
	
	int bytes = (int)(size * sizeof(wchar_t));
	if (_writePos + bytes > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return *this;
	}

	memcpy(_packetBuffer->_buffer + _writePos, value.c_str(), bytes);
	_writePos += bytes;

	return *this;
}

//////////////////////////////////////////////////////////////////////////
// 빼기.
//////////////////////////////////////////////////////////////////////////

Serializer& Serializer::operator>>(char& value)
{
	if (_readPos + sizeof(value) <= _writePos)
	{
		value = *((char*)(_packetBuffer->_buffer + _readPos));
		_readPos += sizeof(value);
	}
	else
	{
		_err = ErrorDeserialize;
	}
	return *this;
}

Serializer& Serializer::operator>>(unsigned char& value)
{
	if (_readPos + sizeof(value) <= _writePos)
	{
		value = *((unsigned char*)(_packetBuffer->_buffer + _readPos));
		_readPos += sizeof(value);
	}
	else
	{
		_err = ErrorDeserialize;
	}
	return *this;
}

Serializer& Serializer::operator>>(short& value)
{
	if (_readPos + sizeof(value) <= _writePos)
	{
		value = *((short*)(_packetBuffer->_buffer + _readPos));
		_readPos += sizeof(value);
	}
	else
	{
		_err = ErrorDeserialize;
	}
	return *this;
}

Serializer& Serializer::operator>>(unsigned short& value)
{
	if (_readPos + sizeof(value) <= _writePos)
	{
		value = *((unsigned short*)(_packetBuffer->_buffer + _readPos));
		_readPos += sizeof(value);
	}
	else
	{
		_err = ErrorDeserialize;
	}
	return *this;
}

Serializer& Serializer::operator>>(int& value)
{
	if (_readPos + sizeof(value) <= _writePos)
	{
		value = *((int*)(_packetBuffer->_buffer + _readPos));
		_readPos += sizeof(value);
	}
	else
	{
		_err = ErrorDeserialize;
	}
	return *this;
}

Serializer& Serializer::operator>>(unsigned int& value)
{
	if (_readPos + sizeof(value) <= _writePos)
	{
		value = *((unsigned int*)(_packetBuffer->_buffer + _readPos));
		_readPos += sizeof(value);
	}
	else
	{
		_err = ErrorDeserialize;
	}
	return *this;
}

Serializer& Serializer::operator>>(long long& value)
{
	if (_readPos + sizeof(value) <= _writePos)
	{
		value = *((long long*)(_packetBuffer->_buffer + _readPos));
		_readPos += sizeof(value);
	}
	else
	{
		_err = ErrorDeserialize;
	}
	return *this;
}

Serializer& Serializer::operator>>(unsigned long long& value)
{
	if (_readPos + sizeof(value) <= _writePos)
	{
		value = *((unsigned long long*)(_packetBuffer->_buffer + _readPos));
		_readPos += sizeof(value);
	}
	else
	{
		_err = ErrorDeserialize;
	}
	return *this;
}

Serializer& Serializer::operator>>(float& value)
{
	if (_readPos + sizeof(value) <= _writePos)
	{
		value = *((float*)(_packetBuffer->_buffer + _readPos));
		_readPos += sizeof(value);
	}
	else
	{
		_err = ErrorDeserialize;
	}
	return *this;
}

Serializer& Serializer::operator>>(double& value)
{
	if (_readPos + sizeof(value) <= _writePos)
	{
		value = *((double*)(_packetBuffer->_buffer + _readPos));
		_readPos += sizeof(value);
	}
	else
	{
		_err = ErrorDeserialize;
	}
	return *this;
}

int Serializer::GetData(char* dest, int size)
{
	if (_readPos + size > _writePos)
	{
		_err = ErrorDeserialize;
		return 0;
	}

	memcpy(dest, _packetBuffer->_buffer + _readPos, size);
	_readPos += size;
	return size;
}

int Serializer::PutData(const char* src, int size)
{
	if (_writePos + size > sizeof(_packetBuffer->_buffer))
	{
		_err = ErrorSerialize;
		return 0;
	}

	memcpy(_packetBuffer->_buffer + _writePos, src, size);
	_writePos += size;
	return size;
}

int Serializer::GetLastError()
{
	return _err;
}

