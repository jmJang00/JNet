#pragma once
#include <JCore/JWindows.h>
#include <JNet/INetworkEntity.h>
#include <JNet/CSession.h>
#include <JNet/Types.h>
#include <atomic>

class CPacketBuffer;
class CWorkerThread;
class CLambdaPipe;

class CNetClient : public INetworkEntity
{
public:
	CNetClient(CWorkerThread* worker);
	virtual ~CNetClient();
	
	bool Connect(const char* bindIp, const char* serverIp, const char* serverPort, bool nagle, bool encoding);
	bool SendPacket(CPacketBuffer* serializer);
	bool Disconnect();
	void Clear();

	virtual bool OnEnterJoinServer() = 0;
	virtual void OnLeaveServer() = 0;
	virtual void OnError(ENetError errCode, const char* errMsg) = 0;
	virtual void OnRecv(FSessionId sessionId, CPacketView* packet) = 0;
	virtual void OnPrintExternal(CMonitorTable* table) {};
	virtual void OnCollectExternal(MetricsCollector& collector) {};

	virtual CSession* CreateSession(SOCKET sock);
	virtual bool ReleaseSession(CSession* session);
	virtual bool Disconnect(FSessionId sessionId);
	bool IsConnected();

	CLambdaPipe* GetClientContext();

protected:
	bool8 _encoding;

private:
	CLambdaPipe* _clientContext;
	bool _isRunning;
	CSession* _session;
	CWorkerThread* _worker;
};
