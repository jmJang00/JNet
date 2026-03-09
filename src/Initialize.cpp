#include "pch.h"
#include <JNet/Initialize.h>
#include <JNet/CLambdaPipe.h>
#include <JCore/SLog.h>
#include <JCore/Initialize.h>
#include "LogTag.h"

long JNetInit::_initLock;
long JNetInit::_init;

DISABLE_WARNINGS_BEGIN(WARNING_28112)

void JNetInit::Initialize()
{
	if (_init == 0)
	{
		while (InterlockedExchange(&_initLock, 1) != 0)
		{
			Sleep(0);
		}

		if (_init == 0)
		{
			JCoreInit::Initialize();
			FLambdaTask::_taskPool = new CTlsMemoryPool<FLambdaTask>(PoolRegistry::RegisterDebugSign("FLambdaTask"), false);
			timeBeginPeriod(1);
			WSADATA wsa;
			if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
			{
				ELOG(JNetLog::Network, L"WSAStartup failed, [ErrorCode]:%d", WSAGetLastError());
				CRASH(true);
			}
			InterlockedExchange(&_init, 1);
		}

		InterlockedExchange(&_initLock, 0);
	}
}

void JNetInit::Release()
{
	if (_init == 1)
	{
		while (InterlockedExchange(&_initLock, 1) != 0)
		{
			Sleep(0);
		}

		if (_init == 1)
		{
			delete FLambdaTask::_taskPool;
			timeEndPeriod(1);
			WSACleanup();
			InterlockedExchange(&_init, 0);
		}

		InterlockedExchange(&_initLock, 0);
	}
}

DISABLE_WARNINGS_END()
