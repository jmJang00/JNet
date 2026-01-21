#pragma once

#pragma pack(push, 1)
struct Header
{
	short size;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct HeaderEx
{
	unsigned char code;
	unsigned short len;
	unsigned char rkey;
	unsigned char checkSum;
};
#pragma pack(pop)

class PacketHeader
{
public:
	static unsigned char SERVER_CODE;
	static unsigned char SERVER_STATIC_KEY;

	static void SetServerCode(unsigned char word);
	static void SetServerStaticKey(unsigned char word);
};

