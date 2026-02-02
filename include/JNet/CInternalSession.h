#pragma once
#include <functional>
#include <utility>
#include <future>
#include <JCore/CLockFreeQueue.h>
#include <JCore/JWindows.h>
#include <JNet/Types.h>
#include <JNet/CWorkerThread.h>

struct FInternalTask
{
	using func_type = void(*)(void*);
	alignas(16) int8 data[64];
	func_type invoke;
	func_type destroy;

	static constexpr int SLOT_SIZE = 64;

	template <typename Lambda>
	static FInternalTask* CreateTask(Lambda&& func)
	{
		FInternalTask* task = _taskPool->Alloc();

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

	static void ReleaseTask(FInternalTask* task)
	{
		task->destroy(task->data);
		_taskPool->Free(task);
	}

	void Invoke()
	{
		invoke(data);
	}

	static CTlsMemoryPool<FInternalTask>* _taskPool;
};

class JNetInit;

class CInternalSession
{
public:
	friend class JNetInit;

	CInternalSession(CWorkerThread* context, int32 maxTaskCnt)
		: _buffer(maxTaskCnt)
		, _context(context)
		, _processing(0)
		, _shutdown(0)
	{
	}

	~CInternalSession()
	{
		ClearPendingTasks();
	}

	template <typename Lambda>
	bool PostLambda(Lambda&& func)
	{
		if (_shutdown)
		{
			return false;
		}

		FInternalTask* task = FInternalTask::CreateTask(std::forward<Lambda>(func));

		if (!PostTask(task))
		{
			FInternalTask::ReleaseTask(task);
			return false;
		}

		return true;
	}

	bool PostTask(FInternalTask* task)
	{
		if (_shutdown)
		{
			return false;
		}

		_buffer.ForceEnqueue(task);

		if (InterlockedExchange8(&_processing, 1) == 0)
		{
			_context->PostStatus((uintptr_t)this, (OVERLAPPED*)CWorkerThread::POST_MESSAGE);
		}

		return true;
	}

	bool PostSync()
	{
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

	void ClearPendingTasks()
	{
		FInternalTask* task;
		while (_buffer.GetSize() > 0)
		{
			if (!_buffer.Dequeue(&task))
			{
				break;
			}

			FInternalTask::ReleaseTask(task);
		}
	}

	void BeginShutdown()
	{
		InterlockedExchange8(&_shutdown, 1);
	}

	void Resume()
	{
		InterlockedExchange8(&_shutdown, 0);
	}

	// 메시지 보내기 전에 플래그를 올릴 수 없음
	// 그렇다고 메시지 보내고 바로 올리면 어느 기준으로 멈추는지 명확하지 않음
	// 메시지가 전달된 시점을 메시지를 처리하는 쪽 기준으로 잡아 
	// 정책이 일관적으로 바뀌도록 조정
	// 주의할 점은 자기 자신을 멈추려고 하면 안 됨
	void Suspend();

	void Execute()
	{
		while (1)
		{
			FInternalTask* task = nullptr;
			_buffer.Dequeue(&task);
			if (task == nullptr)
			{
				break;
			}

			if (_shutdown)
			{
				FInternalTask::ReleaseTask(task);
				continue;
			}

			task->invoke(task->data);
			FInternalTask::ReleaseTask(task);
		}

		InterlockedExchange8(&_processing, 0);

		if (_buffer.GetSize() > 0)
		{
			if (InterlockedExchange8(&_processing, 1) == 0)
			{
				_context->PostStatus((uintptr_t)this, (OVERLAPPED*)CWorkerThread::POST_MESSAGE);
			}
		}
	}

private:
	CRWLock _lock;
	CWorkerThread* _context;
	CLockFreeQueue<FInternalTask*> _buffer;
	int8 _processing;
	int8 _shutdown;
};
