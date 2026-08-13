#include "pch.h"
#include <JNet/CLambdaPipe.h>
#include <JNet/CWorkerThread.h>
#include <JCore/SLog.h>
#include "LogTag.h"

CTlsMemoryPool<FLambdaTask>* FLambdaTask::_taskPool;

CLambdaPipe::CLambdaPipe(CWorkerThread* context, int32 maxTaskCnt)
	: _buffer(maxTaskCnt)
	, _context(context)
	, _processing(0)
	, _enable(0)
{
}

CLambdaPipe::~CLambdaPipe()
{
	ClearPendingTasks();
}

bool CLambdaPipe::PostTask(FLambdaTask* task)
{
	if (_enable)
	{
		return false;
	}

	_buffer.ForceEnqueue(task);

	if (InterlockedExchange8(&_processing, 1) == 0)
	{
		_context->PostStatus((uintptr_t)this, CWorkerThread::sPostMessageOverlapped);
	}

	return true;
}

bool CLambdaPipe::PostSync()
{
	if (_enable)
	{
		return false;
	}

	std::future<bool> f;
	std::shared_ptr<std::promise<bool>> p = std::make_shared<std::promise<bool>>();
	f = p->get_future();

	bool success = PostLambda([p, this]()
		{
			p->set_value(true);
		});

	if (!success)
		return false;

	f.get();

	return true;
}

void CLambdaPipe::ClearPendingTasks()
{
	FLambdaTask* task;
	while (_buffer.GetSize() > 0)
	{
		if (!_buffer.Dequeue(&task))
		{
			break;
		}

		FLambdaTask::ReleaseTask(task);
	}
}

void CLambdaPipe::Disable()
{
	if (!_enable)
	{
		return;
	}

	if (!_context->IsRunning())
	{
		InterlockedExchange8(&_enable, 0);
		ClearPendingTasks();
		return;
	}

	PostLambda([this]()
		{
			InterlockedExchange8(&_enable, 0);
		});
}

void CLambdaPipe::Execute()
{
	while (1)
	{
		FLambdaTask* task = nullptr;
		_buffer.Dequeue(&task);
		if (task == nullptr)
		{
			break;
		}

		if (_enable)
		{
			FLambdaTask::ReleaseTask(task);
			continue;
		}

		task->invoke(task->data);
		FLambdaTask::ReleaseTask(task);
	}

	InterlockedExchange8(&_processing, 0);

	if (_buffer.GetSize() > 0)
	{
		if (InterlockedExchange8(&_processing, 1) == 0)
		{
			_context->PostStatus((uintptr_t)this, CWorkerThread::sPostMessageOverlapped);
		}
	}
}
