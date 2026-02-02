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
	//double procUser;
	//double procKernel;
	//double sysUser;
	//double sysKernel;
	std::vector<std::pair<int, int>>  bucketPool;

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
	//PDH_HCOUNTER procUserCounter;
	//PDH_HCOUNTER procKernelCounter;
	//PDH_HCOUNTER sysUserCounter;
	//PDH_HCOUNTER sysKernelCounter;
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
