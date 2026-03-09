#pragma once
#include <vector>
#include <Pdh.h>
#pragma comment(lib, "Pdh.lib")
#include <JCore/CCpuUsage.h>
#include <JCore/SLog.h>

class CNetServer;

class MetricsCollector
{
public:
	MetricsCollector(std::vector<CNetServer*>& servers, const wchar_t* projectName);
	void Collect();

public:
	DWORD numOfCore;
	time_t recordTime;
	CCpuUsage CPUTime;
	int privateBytes;
	long long nonpagedBytes;
	int workingSetBytes;
	int threadCnt;
	int handleCnt;
	long long virtualBytes;
	int availableMBytes;
	long long poolNonpagedBytes;
	long long committedBytes;
	int sessionIdxAllocCnt;
	int sessionIdxPoolCnt;
	int packetRefAllocCnt;
	int packetRefPoolCnt;
	int packetAllocCnt;
	int packetPoolCnt;
	int sendBufferAllocCnt;
	int sendBufferPoolCnt;
	long long rx;
	long long tx;
	std::vector<std::pair<int, int>> bucketPool;

private:
	std::vector<CNetServer*> _servers;
	PDH_HCOUNTER privateBytesCounter;
	PDH_HCOUNTER nonpagedBytesCounter;
	PDH_HCOUNTER workingSetBytesCounter;
	PDH_HCOUNTER threadCntCounter;
	PDH_HCOUNTER handleCntCounter;
	PDH_HCOUNTER virtualBytesCounter;
	PDH_HCOUNTER availableMBytesCounter;
	PDH_HCOUNTER poolNonpagedBytesCounter;
	PDH_HCOUNTER committedBytesCounter;
	PDH_HCOUNTER rxCounter;
	PDH_HCOUNTER txCounter;
	PDH_HQUERY pdhQuery;
};

class CMonitorThread : public CThread
{
public:
	inline static constexpr int MONITOR_BUFFER_SIZE = 6000;

	CMonitorThread(const wchar_t* projectName, std::vector<CNetServer*>& servers);

	CMonitorThread(const wchar_t* projectName, CNetServer* server);

	~CMonitorThread();

	void Start();

	void Stop();

	void MonitorThread();

private:
	const wchar_t* _projectName;
	std::vector<CNetServer*> _servers;
};

class CMonitorTable
{
public:
    inline static constexpr int MONITOR_BUFFER_SIZE = 12000;

    CMonitorTable(int inColumnWidth);
    
	void PrintColumnFormat(int cnt, const wchar_t* inFormat, ...);
	void PrintColumnStr(int cnt, const wchar_t* inWstr);
	void PrintText(const wchar_t* inFormat, ...);

	void PrintLineFeed();
	void PrintDivider();
	void PrintBoldDivder();
	void Clear();

	wchar_t* Content();

private:
	int columnWidth;
	int column;
    wchar_t monitorBuffer[MONITOR_BUFFER_SIZE];
    wchar_t* wstr;
    size_t remaining;
};
