#include "pch.h"
#include <JNet/Initialize.h>
#include <JNet/CLambdaPipe.h>
#include <JCore/SLog.h>
#include <JCore/Initialize.h>
#include <JNet/CWorkerThread.h>
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

			FLambdaTask::_taskPool = new CTlsMemoryPool<FLambdaTask>("FLambdaTask", false);

			CWorkerThread::sSendStartOverlapped = new FOverlappedEx({ {}, CWorkerThread::SEND_START, 0, 0 });
			CWorkerThread::sReleaseSessionOverlapped = new FOverlappedEx({ {}, CWorkerThread::RELEASE_SESSION, 0, 0 });
			CWorkerThread::sPostMessageOverlapped = new FOverlappedEx({ {}, CWorkerThread::PIPE_POST, 0, 0 });
			CWorkerThread::sPostContentOverlapped = new FOverlappedEx({ {}, CWorkerThread::CONTENT_POST, 0, 0 });
			CWorkerThread::sPostJobOverlapped = new FOverlappedEx({ {}, CWorkerThread::JOB_POST, 0, 0 });

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
			delete CWorkerThread::sSendStartOverlapped;
			delete CWorkerThread::sReleaseSessionOverlapped;
			delete CWorkerThread::sPostMessageOverlapped;
			delete CWorkerThread::sPostContentOverlapped;
			InterlockedExchange(&_init, 0);
		}

		InterlockedExchange(&_initLock, 0);
	}
}

DISABLE_WARNINGS_END()
