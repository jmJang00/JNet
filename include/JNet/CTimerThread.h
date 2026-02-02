#pragma once
#include <vector>
#include <algorithm>
#include <mutex>
#include <JCore/CThread.h>
#include <JCore/ScopedLock.h>
#include <JNet/CInternalSession.h>
#include <JNet/Types.h>
#include <queue>

class CAppServer;
class CContent;

struct FTimerHandle
{
	FTimerHandle(unsigned long long value)
	{
		handle = value;
	}

	FTimerHandle()
	{
		handle = 0;
	}

	enum : unsigned long long
	{
		INVALID_HANDLE = 0xFFFFFFFFFFFFFFFF
	};
	union
	{
		struct
		{
			int index;
			int id;
		};
		unsigned long long handle;
	};
};

struct FTimerNode
{
	using Func = void(CContent::*)(void);

	FTimerHandle handle;
	uint64 reserveMs;
	long active;
	CContent* content;
	Func lambda;

	bool operator<(const FTimerNode& other)
	{
		return reserveMs < other.reserveMs;
	}
};

struct FCompareTimerNode
{
	bool operator()(const FTimerNode* a, const FTimerNode* b) const
	{
		return a->reserveMs > b->reserveMs;
	}
};

class CTimerThread : public CThread
{
public:
	CTimerThread(CAppServer* server);

	static uint64 GetCurrentTick64();

	bool Enqueue(FTimerNode* node);

	void Clear();

	void Shutdown() override;

	void TimerThread();

private:
	HANDLE _hShutdownEvent;
	HANDLE _hEvent;
	CAppServer* _server; 
	uint64 _minMs;
	std::priority_queue<FTimerNode*, std::vector<FTimerNode*>, FCompareTimerNode> _timerQueue;
	CCSLock _lock;
};