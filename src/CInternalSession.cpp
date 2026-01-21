#include "pch.h"
#include <JNet/CInternalSession.h>
#include <JCore/SLog.h>
#include "LogTag.h"

CTlsMemoryPool<FInternalTask>* CInternalSession::_taskPool;

void CInternalSession::Suspend()
{
	if (!_context->IsRunning())
	{
		BeginShutdown();
		ClearPendingTasks();
		ELOG(JNetLog::Network, L"CWorkerThread is not running when the internal session suspends");
		return;
	}

	if (_shutdown)
	{
		return;
	}

	std::future<bool> f;
	std::shared_ptr<std::promise<bool>> p = std::make_shared<std::promise<bool>>();
	f = p->get_future();

	PostLambda([p, this]()
		{
			BeginShutdown();
			p->set_value(true);
		});

	f.get();
}
