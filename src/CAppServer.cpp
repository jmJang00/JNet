#include "pch.h"
#include <JNet/CAppServer.h>
#include <JNet/CTimerManager.h>
#include <JNet/CContentManager.h>

CAppServer::CAppServer(int concurrentThreadCnt, int totalThreadCnt, int maxSession)
	: CNetServer(concurrentThreadCnt, totalThreadCnt, maxSession)
{
	_timerMng = new CTimerManager(this, 20000, 3);
	_contentMng = new CContentManager(this);
}

CAppServer::~CAppServer()
{
	delete _timerMng;
	delete _contentMng;
}

bool CAppServer::Start(const char* ip, const char* port, bool nagle, bool encoding, 
		bool sendBufZero)
{
	do
	{
		_timerMng->Start();

		if (!CNetServer::Start(ip, port, nagle, encoding, sendBufZero))
		{
			return false;
		}

		return true;

	} while (0);

	Stop();

	return false;
}

void CAppServer::Stop()
{
	CNetServer::Stop();

	_timerMng->Stop();
}
