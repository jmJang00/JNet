#pragma once
#include <JCore/JWindows.h>
#include <JNet/Session.h>

class CInternalSession;

enum class NetError
{
	SEND_BUFFER_LIMIT_REACHED,
	INVALID_PACKET_HEADER,
	PAKCET_CHECKSUM_MISMATCH,
	PACKET_SIZE_LIMIT_EXCEEDED,
};

class INetworkEntity
{
public:
	virtual Session* CreateSession(SOCKET sock) = 0;
	virtual bool ReleaseSession(Session* session) = 0;
	virtual void OnRecv(SessionId sessionId, Serializer* packet) = 0;
	virtual void OnError(NetError errCode, const char* errMsg) = 0;
	virtual bool Disconnect(SessionId sessionId) = 0;
	virtual void HandleInternalMessage(CInternalSession* session) = 0;

	virtual ~INetworkEntity() = default;
};
