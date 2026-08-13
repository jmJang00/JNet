#pragma once
#include <JNet/CNetServer.h>

class CTimerManager;
class CContentManager;

class CAppServer : public CNetServer
{
public:
	CAppServer(int concurrentThreadCnt, int totalThreadCnt, int timerThreadCnt, int maxSession = 10000);
	~CAppServer();
	bool Start(const char* ip, const char* port, bool nagle = true, 
			bool encoding = false, bool sendBufZero = true);
	void Stop() override;

	CSession* CreateSession(SOCKET sock) override;

	CTimerManager* TimerMng() { return _timerMng; }
	CContentManager* ContentMng() { return _contentMng; }

private:
	CTimerManager* _timerMng;
	CContentManager* _contentMng;
};
