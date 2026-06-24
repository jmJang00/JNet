#include "pch.h"
#include <time.h>
#include <strsafe.h>
#include <JCore/SLog.h>
#include <JCore/CBucketPool.h>
#include <JNet/CPacket.h>
#include <JNet/CNetServer.h>
#include <JNet/CMonitorThread.h>
#include "LogTag.h"
#include <JCore/CommonDefs.h>

MetricsCollector::MetricsCollector(std::vector<INetworkEntity*>& servers, const wchar_t* projectName)
{
    recordTime = time(nullptr);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    numOfCore = si.dwNumberOfProcessors;

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
    //PDH_FMT_COUNTERVALUE vProcUser, vProcKernel;
    //PDH_FMT_COUNTERVALUE vSysUser, vSysKernel;

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
    //PdhGetFormattedCounterValue(procUserCounter, PDH_FMT_DOUBLE, NULL, &vProcUser);
    //PdhGetFormattedCounterValue(procKernelCounter, PDH_FMT_DOUBLE, NULL, &vProcKernel);
    //PdhGetFormattedCounterValue(sysUserCounter, PDH_FMT_DOUBLE, NULL, &vSysUser);
    //PdhGetFormattedCounterValue(sysKernelCounter, PDH_FMT_DOUBLE, NULL, &vSysKernel);

    privateBytes = privateBytesVal.longValue;
    nonpagedBytes = nonpagedBytesVal.largeValue;
    threadCnt = threadCntVal.longValue;
    handleCnt = handleCntVal.longValue;
    virtualBytes = virtualBytesVal.largeValue;
    workingSetBytes = workingSetBytesVal.longValue;
    availableMBytes = availableMBytesVal.longValue;
    poolNonpagedBytes = poolNonpagedBytesVal.largeValue;
    committedBytes = committedBytesVal.largeValue;
    //procUser = vProcUser.doubleValue / numOfCore;
    //procKernel = vProcKernel.doubleValue / numOfCore;
    //sysUser = vSysUser.doubleValue;
    //sysKernel = vSysKernel.doubleValue;
    rx = rxVal.largeValue;
    tx = txVal.largeValue;

    packetRefPoolCnt = CPacket::GetPoolCount(); 
    packetRefAllocCnt = CPacket::GetAllocCount();
    packetPoolCnt = CPacketBuffer::GetPoolCount(); 
    packetAllocCnt = CPacketBuffer::GetAllocCount();
    sendBufferPoolCnt = CLockFreeQueue<CPacketBuffer*>::GetPoolCount(); 
    sendBufferAllocCnt = CLockFreeQueue<CPacketBuffer*>::GetAllocCount();
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

CMonitorThread::CMonitorThread(const wchar_t* projectName, std::vector<INetworkEntity*>& servers)
    : CThread([this]() { MonitorThread(); }, 1)
{
    _entities = servers;
    _projectName = projectName;
    if (!Create(true))
    {
        ELOG(JNetLog::Network, L"Failed to create monitor thread");
        CRASH(true);
    }
}

CMonitorThread::CMonitorThread(const wchar_t* projectName, INetworkEntity* server)
    : CThread([this]() { MonitorThread(); }, 1)
{
    _entities.push_back(server);
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

CMonitorTable::CMonitorTable(int inColumnWidth)
	: monitorBuffer()
{ 
    columnWidth = inColumnWidth;
    column = 0;
	wstr = monitorBuffer;
	remaining = sizeof(monitorBuffer) / sizeof(wchar_t);
}

void CMonitorTable::PrintColumnFormat(int cnt, const wchar_t* inFormat, ...)
{
	wchar_t buffer[96];
	wchar_t* bufferPtr = buffer;
	size_t bufferRemain = sizeof(buffer) / sizeof(wchar_t);

	va_list list;
	va_start(list, inFormat);
	APPEND_VFORMAT(bufferPtr, bufferRemain, inFormat, list);
	va_end(list);

	int width = 3 * (cnt - 1) + cnt * 9;

    column += cnt;

    if (column >= columnWidth)
    {
        column = 0;
		APPEND_FORMAT(wstr, remaining, L" %-*s \n", width, buffer);
    }
    else
    {
		APPEND_FORMAT(wstr, remaining, L" %-*s |", width, buffer);
    }
}

void CMonitorTable::PrintColumnStr(int cnt, const wchar_t* inWstr)
{
	int width = 3 * (cnt - 1) + cnt * 9;

    column += cnt;

    if (column >= columnWidth)
    {
        column = 0;
		APPEND_FORMAT(wstr, remaining, L" %-*s \n", width, inWstr);
    }
    else
    {
		APPEND_FORMAT(wstr, remaining, L" %-*s |", width, inWstr);
    }
}

void CMonitorTable::PrintText(const wchar_t* inFormat, ...)
{
	va_list list;
	va_start(list, inFormat);
	APPEND_VFORMAT(wstr, remaining, inFormat, list);
	va_end(list);
}

void CMonitorTable::PrintLineFeed()
{
    column = 0;
	APPEND_FORMAT(wstr, remaining, L"\n");
}

void CMonitorTable::PrintDivider()
{
    if (column != 0)
    {
        PrintLineFeed();
    }
	APPEND_FORMAT(wstr, remaining, L"-----------------------------------------------------------------------------------------------\n");
}

void CMonitorTable::PrintBoldDivder()
{
    if (column != 0)
    {
        PrintLineFeed();
    }
	APPEND_FORMAT(wstr, remaining, L"===============================================================================================\n");
}

void CMonitorTable::Clear()
{
    column = 0;
    wstr = monitorBuffer;
    remaining = sizeof(monitorBuffer) / sizeof(wchar_t);
}

wchar_t* CMonitorTable::Content()
{
	return monitorBuffer;
}

bool CMonitorThread::_printMonitor = false;

void CMonitorThread::MonitorThread()
{
    Context* ctxt = CThread::GetContextPtr();
    SLOGA(JNetLog::Network, L"Monitor Thread Start");

    MetricsCollector collector(_entities, _projectName);

    struct tm localTime;
    localtime_s(&localTime, &collector.recordTime);

    wchar_t* filePath = new wchar_t[ProjectConfig::MaxFileNameLength];
    wchar_t* filePtr = filePath;
    size_t remaining = ProjectConfig::MaxFileNameLength;

    APPEND_FORMAT(filePtr, remaining, L"Log\\%04d%02d_%s.txt", localTime.tm_year + 1900, localTime.tm_mon + 1, L"Monitor");

    unsigned int counter = 0;

    DWORD frameTick = timeGetTime();

    CMonitorTable* table = new CMonitorTable(8);

    while (!ctxt->_bShutdown)
    {
        DWORD tick = timeGetTime();
        DWORD deltaTick = tick - frameTick;
        if (deltaTick < 1000)
        {
            Sleep(1000 - deltaTick);
            frameTick += 1000;
        }
        else
        {
            frameTick = tick;
        }

        collector.Collect();
        for (int i = 0; i < _entities.size(); i++)
        {
            _entities[i]->OnCollectExternal(collector);
        }

        if (!_printMonitor)
        {
            continue;
        }

        localtime_s(&localTime, &collector.recordTime);

        table->Clear();
        table->PrintBoldDivder();
        table->PrintText(L"%04d-%02d-%02d %02d:%02d:%02d %s\n", 
            localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
            localTime.tm_hour, localTime.tm_min, localTime.tm_sec, _projectName);
        table->PrintText(L"System Monitoring\n");
        table->PrintDivider();

        table->PrintColumnStr(3, L"[CPU Usage System]");
        table->PrintColumnStr(3, L"[CPU Usage Process]");
        table->PrintColumnStr(2, L"[Rx / Tx]");

        table->PrintColumnFormat(3, L"U:%.2f%% K:%.2f%% T:%.2f%%", collector.CPUTime.ProcessorUser(), collector.CPUTime.ProcessorKernel(), collector.CPUTime.ProcessorTotal());
        table->PrintColumnFormat(3, L"U:%.2f%% K:%.2f%% T:%.2f%%", collector.CPUTime.ProcessUser(),  collector.CPUTime.ProcessKernel(), collector.CPUTime.ProcessTotal());
        table->PrintColumnFormat(2, L"%.2lfKB/s %.2lfKB/s", (double)collector.rx / 1000, (double)collector.tx / 1000);
        table->PrintDivider();

        table->PrintColumnStr(2, L"[Thread / Handle]");
        table->PrintColumnStr(2, L"[Available]");
        table->PrintColumnStr(2, L"[Committed]");
        table->PrintColumnStr(2, L"[System Nonpaged]");

        table->PrintColumnFormat(2, L"%d %d", collector.threadCnt, collector.handleCnt);
        table->PrintColumnFormat(2, L"%.2lfMB", (double)collector.availableMBytes);
        table->PrintColumnFormat(2, L"%.2lfMB", (double)collector.committedBytes / 1000000);
        table->PrintColumnFormat(2, L"%.2lfMB", (double)collector.poolNonpagedBytes / 1000000);

        table->PrintColumnStr(2, L"[Process Nonpaged]");
        table->PrintColumnStr(2, L"[Virtual Bytes]");
        table->PrintColumnStr(2, L"[Working Set]");
        table->PrintColumnStr(2, L"[Private Bytes]");

        table->PrintColumnFormat(2, L"%.2lfMB", (double)collector.nonpagedBytes / 1000000);
        table->PrintColumnFormat(2, L"%.2lfMB", (double)collector.virtualBytes / 1000000);
        table->PrintColumnFormat(2, L"%.2lfMB", (double)collector.workingSetBytes / 1000000);
        table->PrintColumnFormat(2, L"%.2lfMB", (double)collector.privateBytes / 1000000);

        table->PrintColumnStr(2, L"[Packet Buffer Ref]");
        table->PrintColumnStr(2, L"[Packet Buffer]");
        table->PrintColumnStr(2, L"[Send Queue Node]");
        table->PrintColumnStr(2, L"[Session Idx Node]");

        table->PrintColumnFormat(2, L"P:%d A:%d", collector.packetRefPoolCnt, collector.packetRefAllocCnt);
        table->PrintColumnFormat(2, L"P:%d A:%d", collector.packetPoolCnt, collector.packetAllocCnt);
        table->PrintColumnFormat(2, L"P:%d A:%d", collector.sendBufferPoolCnt, collector.sendBufferAllocCnt);
        table->PrintColumnFormat(2, L"P:%d A:%d", collector.sessionIdxAllocCnt, collector.sessionIdxPoolCnt);

        table->PrintDivider();
        table->PrintColumnFormat(1, L"32-%d", collector.bucketPool[0].second);
        table->PrintColumnFormat(1, L"64-%d", collector.bucketPool[1].second);
        table->PrintColumnFormat(1, L"128-%d", collector.bucketPool[2].second);
        table->PrintColumnFormat(1, L"256-%d", collector.bucketPool[3].second);
        table->PrintColumnFormat(1, L"512-%d", collector.bucketPool[4].second);
        table->PrintLineFeed();

        table->PrintText(L"\nServer Monitoring\n");
        table->PrintDivider();
        for (int i = 0; i < _entities.size(); i++)
        {
            INetworkEntity* server = _entities[i];
            server->OnPrintExternal(table);
        }
        table->PrintBoldDivder();

        counter++;
        if (counter % 180 == 0)
        {
            Logger::WriteLogFile(filePath, table->Content(), MONITOR_BUFFER_SIZE - (int)remaining);
        }
        printf("\n\n%ls", table->Content());
    }

    delete[] filePath;
    SLOGA(JNetLog::Network, L"Monitor Thread Exit");
}

