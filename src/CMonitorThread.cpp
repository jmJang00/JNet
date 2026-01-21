#include "pch.h"
#include <time.h>
#include <strsafe.h>
#include <JCore/SLog.h>
#include <JCore/CBucketPool.h>
#include <JNet/Serializer.h>
#include <JNet/CNetServer.h>
#include <JNet/CMonitorThread.h>
#include "LogTag.h"
#include <JCore/CommonDefs.h>

MetricsCollector::MetricsCollector(std::vector<CNetServer*>& servers, const wchar_t* projectName)
{
	recordTime = time(nullptr);
	wchar_t assembleBuffer[ProjectConfig::AssembleBufferSize];
	wchar_t* buffer = assembleBuffer;
	buffer[0] = L'\0';
	size_t remaining = sizeof(assembleBuffer);
	PdhOpenQuery(NULL, NULL, &pdhQuery);
	APPEND_FORMAT(buffer, remaining, L"\\Process(%s)\\Private Bytes", projectName);
	PdhAddCounter(pdhQuery, assembleBuffer, NULL, &privateBytesCounter);

	buffer = assembleBuffer;
	buffer[0] = L'\0';
	remaining = sizeof(assembleBuffer);
	APPEND_FORMAT(buffer, remaining, L"\\Process(%s)\\Pool Nonpaged Bytes", projectName);
	PdhAddCounter(pdhQuery, assembleBuffer, NULL, &nonpagedBytesCounter);

	buffer = assembleBuffer;
	buffer[0] = L'\0';
	remaining = sizeof(assembleBuffer);
	APPEND_FORMAT(buffer, remaining, L"\\Process(%s)\\Working Set", projectName);
	PdhAddCounter(pdhQuery, assembleBuffer, NULL, &workingSetBytesCounter);

	buffer = assembleBuffer;
	buffer[0] = L'\0';
	remaining = sizeof(assembleBuffer);
	APPEND_FORMAT(buffer, remaining, L"\\Process(%s)\\Thread Count", projectName);
	PdhAddCounter(pdhQuery, assembleBuffer, NULL, &threadCntCounter);

	buffer = assembleBuffer;
	buffer[0] = L'\0';
	remaining = sizeof(assembleBuffer);
	APPEND_FORMAT(buffer, remaining, L"\\Process(%s)\\Handle Count", projectName);
	PdhAddCounter(pdhQuery, assembleBuffer, NULL, &handleCntCounter);

	buffer = assembleBuffer;
	buffer[0] = L'\0';
	remaining = sizeof(assembleBuffer);
	APPEND_FORMAT(buffer, remaining, L"\\Process(%s)\\Virtual Bytes", projectName);
	PdhAddCounter(pdhQuery, assembleBuffer, NULL, &virtualBytesCounter);

	PdhAddCounter(pdhQuery, L"\\Memory\\Available MBytes", NULL, &availableMBytesCounter);
	PdhAddCounter(pdhQuery, L"\\Memory\\Pool Nonpaged Bytes", NULL, &poolNonpagedBytesCounter);
	PdhAddCounter(pdhQuery, L"\\Memory\\Committed Bytes", NULL, &committedBytesCounter);
	PdhAddCounter(pdhQuery, L"\\Network Interface(*)\\Bytes Received/sec", 0, &rxCounter);
	PdhAddCounter(pdhQuery, L"\\Network Interface(*)\\Bytes Sent/sec", 0, &txCounter);

	privateBytes = 0;
	nonpagedBytes = 0;
	workingSetBytes = 0;
	threadCnt = 0;
	handleCnt = 0;
	virtualBytes = 0;
	availableMBytes = 0;
	poolNonpagedBytes = 0;
	committedBytes = 0;
	sessionIdxAllocCnt = 0;
	sessionIdxPoolCnt = 0;
	packetRefAllocCnt = 0;
	packetRefPoolCnt = 0;
	packetAllocCnt = 0;
	packetPoolCnt = 0;
	sendBufferAllocCnt = 0;
	sendBufferPoolCnt = 0;
	rx = 0;
	tx = 0;
	bucketPool.resize(5, std::make_pair<int, int>(0, 0));
}

void MetricsCollector::Collect()
{
	PDH_FMT_COUNTERVALUE privateBytesVal;
	PDH_FMT_COUNTERVALUE nonpagedBytesVal;
	PDH_FMT_COUNTERVALUE threadCntVal;
	PDH_FMT_COUNTERVALUE handleCntVal;
	PDH_FMT_COUNTERVALUE virtualBytesVal;
	PDH_FMT_COUNTERVALUE workingSetBytesVal;
	PDH_FMT_COUNTERVALUE availableMBytesVal;
	PDH_FMT_COUNTERVALUE poolNonpagedBytesVal;
	PDH_FMT_COUNTERVALUE committedBytesVal;
	PDH_FMT_COUNTERVALUE rxVal;
	PDH_FMT_COUNTERVALUE txVal;

	PdhCollectQueryData(pdhQuery);

	PdhGetFormattedCounterValue(privateBytesCounter, PDH_FMT_LONG, NULL, &privateBytesVal);
	PdhGetFormattedCounterValue(nonpagedBytesCounter, PDH_FMT_LARGE, NULL, &nonpagedBytesVal);
	PdhGetFormattedCounterValue(workingSetBytesCounter, PDH_FMT_LONG, NULL, &workingSetBytesVal);
	PdhGetFormattedCounterValue(threadCntCounter, PDH_FMT_LONG, NULL, &threadCntVal);
	PdhGetFormattedCounterValue(handleCntCounter, PDH_FMT_LONG, NULL, &handleCntVal);
	PdhGetFormattedCounterValue(virtualBytesCounter, PDH_FMT_LARGE, NULL, &virtualBytesVal);
	PdhGetFormattedCounterValue(availableMBytesCounter, PDH_FMT_LONG, NULL, &availableMBytesVal);
	PdhGetFormattedCounterValue(poolNonpagedBytesCounter, PDH_FMT_LARGE, NULL, &poolNonpagedBytesVal);
	PdhGetFormattedCounterValue(committedBytesCounter, PDH_FMT_LARGE, NULL, &committedBytesVal);
	PdhGetFormattedCounterValue(rxCounter, PDH_FMT_LARGE, NULL, &rxVal);
	PdhGetFormattedCounterValue(txCounter, PDH_FMT_LARGE, NULL, &txVal);

	privateBytes = privateBytesVal.longValue;
	nonpagedBytes = nonpagedBytesVal.largeValue;
	threadCnt = threadCntVal.longValue;
	handleCnt = handleCntVal.longValue;
	virtualBytes = virtualBytesVal.largeValue;
	workingSetBytes = workingSetBytesVal.longValue;
	availableMBytes = availableMBytesVal.longValue;
	poolNonpagedBytes = poolNonpagedBytesVal.largeValue;
	committedBytes = committedBytesVal.largeValue;
	rx = rxVal.largeValue;
	tx = txVal.largeValue;

	packetRefPoolCnt = Serializer::GetPoolCount(); 
	packetRefAllocCnt = Serializer::GetAllocCount();
	packetPoolCnt = PacketBuffer::GetPoolCount(); 
	packetAllocCnt = PacketBuffer::GetAllocCount();
	sendBufferPoolCnt = CLockFreeQueue<Serializer*>::GetPoolCount(); 
	sendBufferAllocCnt = CLockFreeQueue<Serializer*>::GetAllocCount();
	sessionIdxAllocCnt = CLockFreeStack<int>::GetPoolCount(); 
	sessionIdxPoolCnt = CLockFreeStack<int>::GetAllocCount();

	bucketPool[0].first = gBucketPool->GetPoolCount(32);
	bucketPool[0].second = gBucketPool->GetAllocCount(32); 
	bucketPool[1].first = gBucketPool->GetPoolCount(64);
	bucketPool[1].second = gBucketPool->GetAllocCount(64);
	bucketPool[2].first = gBucketPool->GetPoolCount(128); 
	bucketPool[2].second = gBucketPool->GetAllocCount(128); 
	bucketPool[3].first = gBucketPool->GetPoolCount(256); 
	bucketPool[3].second = gBucketPool->GetAllocCount(256);
	bucketPool[4].first = gBucketPool->GetPoolCount(512); 
	bucketPool[4].second = gBucketPool->GetAllocCount(512);

	CPUTime.UpdateCpuTime();

	recordTime = time(nullptr);
}

CMonitorThread::CMonitorThread(const wchar_t* projectName, std::vector<CNetServer*>& servers)
	: CThread([this]() { MonitorThread(); }, 1)
{
	_servers = servers;
	_projectName = projectName;
	if (!Create(true))
	{
		ELOG(JNetLog::Network, L"Failed to create monitor thread");
		CRASH(true);
	}
}

CMonitorThread::CMonitorThread(const wchar_t* projectName, CNetServer* server)
	: CThread([this]() { MonitorThread(); }, 1)
{
	_servers.push_back(server);
	_projectName = projectName;
	if (!Create(true))
	{
		ELOG(JNetLog::Network, L"Failed to create monitor thread");
		CRASH(true);
	}
}

CMonitorThread::~CMonitorThread()
{
	Close();
}

void CMonitorThread::Start()
{
	Resume();
}

void CMonitorThread::Stop()
{
	Shutdown();
	Wait();
}

void CMonitorThread::MonitorThread()
{
	Context* ctxt = CThread::GetContextPtr();
	SLOGA(JNetLog::Network, L"Monitor Thread Start\n");

	MetricsCollector collector(_servers, _projectName);

	struct tm localTime;
	localtime_s(&localTime, &collector.recordTime);

	wchar_t* monitorBuffer = new wchar_t[MONITOR_BUFFER_SIZE];
	wchar_t* filePath = new wchar_t[ProjectConfig::MaxFileNameLength];
	wchar_t* filePtr = filePath;
	size_t remaining = ProjectConfig::MaxFileNameLength;

	APPEND_FORMAT(filePtr, remaining, L"Log\\%04d%02d_%s.txt", localTime.tm_year + 1900, localTime.tm_mon + 1, L"Monitor");

	unsigned int counter = 0;

	DWORD frameTick = timeGetTime();

	while (!ctxt->_bShutdown)
	{
		DWORD tick = timeGetTime();
		DWORD deltaTick = tick - frameTick;
		if (deltaTick < 1000)
		{
			Sleep(1000 - deltaTick);
		}
		frameTick += 1000;

		collector.Collect();
		for (int i = 0; i < _servers.size(); i++)
		{
			_servers[i]->OnCollectExternal(collector);
		}

		if (Logger::GetPrintLogLevel() == 0)
		{
			continue;
		}

		localtime_s(&localTime, &collector.recordTime);

		remaining = MONITOR_BUFFER_SIZE;
		wchar_t* wstr = monitorBuffer;

		APPEND_FORMAT(wstr, remaining, L"\n");
		APPEND_FORMAT(wstr, remaining, L"===============================================================================================\n");

		APPEND_FORMAT(wstr, remaining, L"%04d-%02d-%02d %02d:%02d:%02d %s\n", 
			localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
			localTime.tm_hour, localTime.tm_min, localTime.tm_sec, _projectName);

		APPEND_FORMAT(wstr, remaining, L"System Monitoring\n");

		APPEND_FORMAT(wstr, remaining, L"-----------------------------------------------------------------------------------------------\n");
		APPEND_FORMAT(wstr, remaining, L" %-29s | %-29s | %-29s \n", 
			L"[CPU Usage Process]", L"[CPU Usage System]", L"[Recv Bytes / Send Bytes]");

		APPEND_FORMAT(wstr, remaining, L" U:%6.2f%% K:%6.2f%% T:%6.2f%% | U:%6.2f%% K:%6.2f%% T:%6.2f%% | %-10.3lfKB/s %-10.3lfKB/s \n", 
			collector.CPUTime.ProcessorUser(),  collector.CPUTime.ProcessorKernel(), collector.CPUTime.ProcessorTotal(), 
			collector.CPUTime.ProcessUser(), collector.CPUTime.ProcessKernel(),  collector.CPUTime.ProcessTotal(), 
			(double)collector.rx / 1000, (double)collector.tx / 1000);
		APPEND_FORMAT(wstr, remaining, L"-----------------------------------------------------------------------------------------------\n");


		APPEND_FORMAT(wstr, remaining, L" %-21s | %-21s | %-21s | %-21s\n", 
			L"[Thread / Handle]", L"[Available]", L"[Committed]", L"[System Nonpaged]");
		APPEND_FORMAT(wstr, remaining, L" %-10d %-10d | %-19.2lfMB | %-19.2fMB | %-19.2lfMB\n", 
			collector.threadCnt, collector.handleCnt, (double)collector.availableMBytes, (double)collector.committedBytes / 1000000, (double)collector.poolNonpagedBytes / 1000000);

		APPEND_FORMAT(wstr, remaining, L" %-21s | %-21s | %-21s | %-21s\n", 
			L"[Process Nonpaged]", L"[Virtual Bytes]", L"[Working Set]", L"[Private Bytes]");
		APPEND_FORMAT(wstr, remaining, L" %-19.2lfMB | %-19.2lfMB | %-19.2lfMB | %-19.2lfMB\n", 
			(double)collector.nonpagedBytes / 1000000, (double)collector.virtualBytes / 1000000, (double)collector.workingSetBytes / 1000000, (double)collector.privateBytes / 1000000);

		APPEND_FORMAT(wstr, remaining, L" %-21s | %-21s | %-21s | %-21s\n", 
			L"[Packet Buffer Ref]", L"[Packet Buffer]", L"[Send Queue Node]", L"[Session Idx Node]");
		APPEND_FORMAT(wstr, remaining, L" P:%-8d A:%-8d | P:%-8d A:%-8d | P:%-8d A:%-8d | P:%-8d A:%-8d\n", 
			collector.packetRefPoolCnt, collector.packetRefAllocCnt, 
			collector.packetPoolCnt, collector.packetAllocCnt,
			collector.sendBufferPoolCnt, collector.sendBufferAllocCnt, 
			collector.sessionIdxAllocCnt, collector.sessionIdxPoolCnt);

		APPEND_FORMAT(wstr, remaining, L"-----------------------------------------------------------------------------------------------\n");
		APPEND_FORMAT(wstr, remaining, L" 032 A:%-10d | 064 A:%-10d | 128 A:%-10d | 256 A:%-10d | 512 A:%-10d \n",
			collector.bucketPool[0].second,
			collector.bucketPool[1].second,
			collector.bucketPool[2].second,
			collector.bucketPool[3].second,
			collector.bucketPool[4].second);

		APPEND_FORMAT(wstr, remaining, L"\nServer Monitoring P=(Global)Pool A=Alloc\n");
		for (int i = 0; i < _servers.size(); i++)
		{
			CNetServer* server = _servers[i];
			server->OnPrintExternal(&wstr, &remaining);
		}
		APPEND_FORMAT(wstr, remaining, L"===============================================================================================\n");
		APPEND_FORMAT(wstr, remaining, L"\n");

		counter++;
		if (counter % 180 == 0)
		{
			Logger::WriteLogFile(filePath, monitorBuffer, MONITOR_BUFFER_SIZE - (int)remaining);
		}
		printf("%ls", monitorBuffer);
	}

	for (int i = 0; i < _servers.size(); i++)
	{
		_servers[i]->RefreshStatistics();
	}

	delete[] monitorBuffer;
	delete[] filePath;
	SLOGA(JNetLog::Network, L"Monitor Thread Exit\n");
}

