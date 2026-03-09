#pragma once

#pragma pack(push, 1)
struct FHeader
{
	unsigned short size;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct FHeaderEx
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

