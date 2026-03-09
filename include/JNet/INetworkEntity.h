#pragma once
#include <JCore/JWindows.h>
#include <JNet/CSession.h>

class CLambdaPipe;
class CPacketView;

enum class ENetError
{
	SEND_BUFFER_LIMIT_REACHED,
	INVALID_PACKET_HEADER,
	PAKCET_CHECKSUM_MISMATCH,
	PACKET_SIZE_LIMIT_EXCEEDED,
	RECV_UNKNOWN_DEST_PACKET,
};

class INetworkEntity
{
public:
	virtual CSession* CreateSession(SOCKET sock) = 0;
	virtual bool ReleaseSession(CSession* session) = 0;
	virtual void OnRecv(FSessionId sessionId, CPacketView* packet) = 0;
	virtual void OnError(ENetError errCode, const char* errMsg) = 0;
	virtual bool Disconnect(FSessionId sessionId) = 0;

	virtual ~INetworkEntity() = default;
};
