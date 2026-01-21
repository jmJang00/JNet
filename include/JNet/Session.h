#pragma once
#include <JCore/JWindows.h>
#include <JCore/CLockFreeQueue.h>
#include <JCore/SLog.h>

class Session;
class Serializer;
class CNetServer;
class INetworkEntity;
class CLoginThread;
class MonitorNode;
class CContent;

struct OverlappedEx
{
	OVERLAPPED overlapped;
	unsigned int reqLen;
	unsigned int bufCnt;
	Session* session;
};

union SessionId
{
	SessionId() = default;

	SessionId(unsigned long long id)
	{
		total = id;
	}

	struct u
	{
		unsigned int idx;
		unsigned int id;
	};
	u internal;
	unsigned long long total;

	bool operator<(const SessionId& other) const
	{
		return total < other.total;
	}

	bool operator==(const SessionId& other)
	{
		return total == other.total;
	}

	bool operator==(const SessionId& other) const
	{
		return total == other.total;
	}

	bool operator!=(const SessionId& other) const
	{
		return total != other.total;
	}
};

namespace std
{
	template <>
	struct hash<SessionId>
	{
		size_t operator()(const SessionId& p) const
		{
			return hash<unsigned long long>{}(p.total);
		}
	};
}


class Session
{
public:
	static constexpr int SND_BUFSIZE = 8096;
	static constexpr int RCV_BUFSIZE = 1024;
	static constexpr int NETWORK_PACKET_RECV_TIMEOUT = 40000;
	static constexpr int PENDING_BUFFER_SIZE = 100;
	static constexpr unsigned long long INVALID_SESSION_ID = 0xffffffffffffffff;

	Session();

	Session(INetworkEntity* parent);

	~Session();

	void Start(SOCKET socket, HANDLE iocp, INetworkEntity* caller, SessionId sessionId, bool crypt);

	void Reset();

	bool SendPost();

	bool RecvPost();

	bool AddRef();

	bool Release();

	bool ReleasePost();

	SessionId id = { 0 };
	INetworkEntity* owner = nullptr;
	OverlappedEx* sendOverlapped = nullptr;
	OverlappedEx* recvOverlapped = nullptr;
	SOCKET sock = INVALID_SOCKET;
	HANDLE hIOCP = NULL;
	WCHAR ip[16] = { 0 };
	unsigned int port = 0;
	CLockFreeQueue<Serializer*> sendBuf;
	CLockFreeQueue<Serializer*> contentQ;
	CContent* content;
	Serializer* recvBuf = nullptr;
	CLoginThread* dest = nullptr;
	long assembleCnt = 0;
	long refCnt = 0;
	long sending = 0;
	long invalid = 0;
	long disconnect = 0;
	bool encoding = false;
	Serializer** pendingBuffer;
	void* user;
};

extern thread_local std::vector<SessionId> gSessionIdVector;
