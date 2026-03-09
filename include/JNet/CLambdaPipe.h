#pragma once
#include <functional>
#include <utility>
#include <future>
#include <JCore/CLockFreeQueue.h>
#include <JCore/JWindows.h>
#include <JNet/Types.h>
#include <JNet/CWorkerThread.h>

struct FLambdaTask
{
	using func_type = void(*)(void*);
	alignas(16) int8 data[64];
	func_type invoke;
	func_type destroy;

	static constexpr int SLOT_SIZE = 64;

	template <typename Lambda>
	static FLambdaTask* CreateTask(Lambda&& func)
	{
		FLambdaTask* task = _taskPool->Alloc();

		static_assert(sizeof(Lambda) <= SLOT_SIZE);

		new (&task->data) Lambda(std::forward<Lambda>(func));

		task->destroy = [](void* ptr)
			{
				((Lambda*)ptr)->~Lambda();
			};

		task->invoke = [](void* ptr)
			{
				(*(Lambda*)ptr)();
			};

		return task;
	}

	static void ReleaseTask(FLambdaTask* task)
	{
		task->destroy(task->data);
		_taskPool->Free(task);
	}

	void Invoke()
	{
		invoke(data);
	}

	static CTlsMemoryPool<FLambdaTask>* _taskPool;
};

class JNetInit;

class CLambdaPipe
{
public:
	friend class JNetInit;

	CLambdaPipe(CWorkerThread* context, int32 maxTaskCnt);

	~CLambdaPipe();

	template <typename Lambda>
	bool PostLambda(Lambda&& func)
	{
		if (_enable)
		{
			return false;
		}

		FLambdaTask* task = FLambdaTask::CreateTask(std::forward<Lambda>(func));

		if (!PostTask(task))
		{
			FLambdaTask::ReleaseTask(task);
			return false;
		}

		return true;
	}

	bool PostTask(FLambdaTask* task);

	bool PostSync();

	void ClearPendingTasks();

	void Enable() { InterlockedExchange8(&_enable, 1); }

	void Disable();

	void Execute();

private:
	CWorkerThread* _context;
	CLockFreeQueue<FLambdaTask*> _buffer;
	int8 _processing;
	int8 _enable;
};
