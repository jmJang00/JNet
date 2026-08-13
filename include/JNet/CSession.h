#pragma once
#include <array>
#include <JCore/JWindows.h>
#include <JCore/CLockFreeQueue.h>
#include <JCore/SLog.h>
#include <JNet/FContentHandle.h>
#include <JCore/CRingBuffer.h>
#include <JNet/CPacketView.h>

class CPacketBuffer;
class INetworkEntity;
class CContentManager;
class CWorkerThread;

struct FOverlappedEx
{
	OVERLAPPED overlapped;
	unsigned short type;
	unsigned int reqLen;
	unsigned int bufCnt;
};

struct FSessionId
{
	FSessionId() = default;

	FSessionId(unsigned long long id)
	{
		total = id;
	}

	union
	{
		struct 
		{
			unsigned int idx;
			unsigned int id;
		} internal;
		unsigned long long total;
	};

	bool operator<(const FSessionId& other) const
	{
		return total < other.total;
	}

	bool operator==(const FSessionId& other)
	{
		return total == other.total;
	}

	bool operator==(const FSessionId& other) const
	{
		return total == other.total;
	}

	bool operator!=(const FSessionId& other) const
	{
		return total != other.total;
	}
};

namespace std
{
	template <>
	struct hash<FSessionId>
	{
		size_t operator()(const FSessionId& p) const
		{
			return hash<unsigned long long>{}(p.total);
		}
	};
}

//#define SESSION_DEBUG 

struct FSessionAddress
{
	WCHAR ip[16] = { 0 };
	unsigned int port = 0;
};

class CSession
{
public:
	static constexpr int SND_BUFSIZE = 8096;
	static constexpr int RCV_BUFSIZE = 1024;
	static constexpr int NETWORK_PACKET_RECV_TIMEOUT = 40000;
	static constexpr int PENDING_BUFFER_SIZE = 100;
	static constexpr unsigned long long INVALID_SESSION_ID = 0xffffffffffffffff;

	CSession();

	CSession(INetworkEntity* parent);

	~CSession();

	void Start(SOCKET socket, CWorkerThread* iocpWorker, INetworkEntity* sessionOwner, 
		CContentManager* contentMng, FSessionId sessionId, bool encoding);

	void Reset();

	bool SendPostRaw();

	bool SendPost();

	bool RecvPost();

	bool AddRef();

	bool Release();

	bool ReleasePost();

	FSessionId _id = { 0 };
	long _refCnt = 0;
	char _disconnect = 0;
	char _sending = 0;
	char _invalid = 0;
	bool _encoding = false;
	INetworkEntity* _owner = nullptr;
	FOverlappedEx* _sendOverlapped = nullptr;
	FOverlappedEx* _recvOverlapped = nullptr;
	SOCKET _sock = INVALID_SOCKET;
	CWorkerThread* _worker = nullptr;
	FSessionAddress* _address;
	CLockFreeQueue<CPacketBuffer*>* _sendBuf;
	CRingBuffer* _contentQ;
	FContentHandle _content;
	CPacketBuffer* _recvBuf;
	CContentManager* _contentMng = nullptr;
	long _assembleCnt = 0;
	std::vector<CPacketBuffer*> _pendingBuffer;
	void* _userData;
};

extern thread_local std::vector<FSessionId> gSessionIdVector;
