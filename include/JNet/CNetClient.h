#pragma once
#include <JCore/JWindows.h>
#include <JNet/INetworkEntity.h>
#include <JNet/Session.h>
#include <JNet/Types.h>
#include <atomic>

class Serializer;
class CWorkerThread;
class CInternalSession;

class CNetClient : public INetworkEntity
{
public:
	CNetClient(CWorkerThread* worker);
	virtual ~CNetClient();
	
	bool Connect(const char* bindIp, const char* serverIp, const char* serverPort, bool nagle, bool encoding);
	bool SendPacket(Serializer* serializer);
	bool Disconnect();
	void Clear();

	virtual bool OnEnterJoinServer() = 0;
	virtual void OnLeaveServer() = 0;
	virtual void OnError(NetError errCode, const char* errMsg) = 0;
	virtual void OnRecv(SessionId sessionId, Serializer* packet) = 0;

	virtual Session* CreateSession(SOCKET sock);
	virtual bool ReleaseSession(Session* session);
	virtual bool Disconnect(SessionId sessionId);
	bool IsConnected();

	CInternalSession* GetClientContext();

protected:
	bool8 _encoding;

private:
	CInternalSession* _clientContext;
	bool _isRunning;
	SOCKET _sock;
	Session* _session;
	CWorkerThread* _worker;
};
