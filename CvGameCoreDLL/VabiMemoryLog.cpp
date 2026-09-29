// Memory log (ported from VabiGEM): writes one line to Logs\VabiMemory.log whenever the active player's turn starts
// (also during AI auto-play, when the AI plays the active player's civilization)
// (switch: VABI_MEMORY_LOG in GlobalDefinesAlt.xml; the game only writes logs with LoggingEnabled = 1 in
// CivilizationIV.ini). Civ4 is a 32 bit program: with the large address aware flag it gets 4 GB of address
// space on 64 bit Windows, and allocations fail once no large enough free block is left, so the log shows
// both the address space in use and the largest free block. The read/write columns are the file I/O of the
// whole turn cycle since the previous line (includes reads served from the Windows file cache).
#include "CvGameCoreDLL.h"
#include <malloc.h>

namespace
{
	// own copies of the Windows structures, so this does not depend on the SDK / _WIN32_WINNT version
	struct VabiMemoryStatusEx
	{
		DWORD dwLength;
		DWORD dwMemoryLoad;
		DWORDLONG ullTotalPhys;
		DWORDLONG ullAvailPhys;
		DWORDLONG ullTotalPageFile;
		DWORDLONG ullAvailPageFile;
		DWORDLONG ullTotalVirtual;
		DWORDLONG ullAvailVirtual;
		DWORDLONG ullAvailExtendedVirtual;
	};

	struct VabiProcessMemoryCounters
	{
		DWORD cb;
		DWORD PageFaultCount;
		SIZE_T PeakWorkingSetSize;
		SIZE_T WorkingSetSize;
		SIZE_T QuotaPeakPagedPoolUsage;
		SIZE_T QuotaPagedPoolUsage;
		SIZE_T QuotaPeakNonPagedPoolUsage;
		SIZE_T QuotaNonPagedPoolUsage;
		SIZE_T PagefileUsage;		// private bytes
		SIZE_T PeakPagefileUsage;
	};

	struct VabiIoCounters
	{
		ULONGLONG ReadOperationCount;
		ULONGLONG WriteOperationCount;
		ULONGLONG OtherOperationCount;
		ULONGLONG ReadTransferCount;
		ULONGLONG WriteTransferCount;
		ULONGLONG OtherTransferCount;
	};

	typedef BOOL (WINAPI *GlobalMemoryStatusExFn)(VabiMemoryStatusEx*);
	typedef BOOL (WINAPI *GetProcessMemoryInfoFn)(HANDLE, VabiProcessMemoryCounters*, DWORD);
	typedef BOOL (WINAPI *GetProcessIoCountersFn)(HANDLE, VabiIoCounters*);

	bool s_bInit = false;
	GlobalMemoryStatusExFn s_pGlobalMemoryStatusEx = NULL;
	GetProcessMemoryInfoFn s_pGetProcessMemoryInfo = NULL;
	GetProcessIoCountersFn s_pGetProcessIoCounters = NULL;

	bool s_bHaveLast = false;
	DWORD s_iLastPageFaults = 0;
	VabiIoCounters s_kLastIo;

	void init()
	{
		s_bInit = true;
		HMODULE hKernel = GetModuleHandleA("kernel32.dll");
		if (hKernel != NULL)
		{
			s_pGlobalMemoryStatusEx = (GlobalMemoryStatusExFn)GetProcAddress(hKernel, "GlobalMemoryStatusEx");
			s_pGetProcessIoCounters = (GetProcessIoCountersFn)GetProcAddress(hKernel, "GetProcessIoCounters");
			// Windows 7+ also exports it from kernel32 as K32GetProcessMemoryInfo
			s_pGetProcessMemoryInfo = (GetProcessMemoryInfoFn)GetProcAddress(hKernel, "K32GetProcessMemoryInfo");
		}
		if (s_pGetProcessMemoryInfo == NULL)
		{
			HMODULE hPsapi = LoadLibraryA("psapi.dll");
			if (hPsapi != NULL)
			{
				s_pGetProcessMemoryInfo = (GetProcessMemoryInfoFn)GetProcAddress(hPsapi, "GetProcessMemoryInfo");
			}
		}
	}

	struct VabiAddressSpace
	{
		SIZE_T iLargestFree;		// largest free (unreserved) block
		SIZE_T iCommittedPrivate;	// heaps and other allocations of the DLL, Python, the engine and drivers
		SIZE_T iCommittedMapped;	// memory mapped files and shared sections
		SIZE_T iCommittedImage;		// loaded exe and dll code/data
	};

	// walks the process address space once, in bytes
	VabiAddressSpace scanAddressSpace(SIZE_T iAddressLimit)
	{
		MEMORY_BASIC_INFORMATION kInfo;
		VabiAddressSpace kSpace = { 0, 0, 0, 0 };
		SIZE_T iAddress = 0;
		while (iAddress < iAddressLimit && VirtualQuery((LPCVOID)iAddress, &kInfo, sizeof(kInfo)) == sizeof(kInfo))
		{
			if (kInfo.State == MEM_FREE && kInfo.RegionSize > kSpace.iLargestFree)
			{
				kSpace.iLargestFree = kInfo.RegionSize;
			}
			else if (kInfo.State == MEM_COMMIT)
			{
				if (kInfo.Type == MEM_PRIVATE)
				{
					kSpace.iCommittedPrivate += kInfo.RegionSize;
				}
				else if (kInfo.Type == MEM_MAPPED)
				{
					kSpace.iCommittedMapped += kInfo.RegionSize;
				}
				else if (kInfo.Type == MEM_IMAGE)
				{
					kSpace.iCommittedImage += kInfo.RegionSize;
				}
			}
			SIZE_T iNext = (SIZE_T)kInfo.BaseAddress + kInfo.RegionSize;
			if (iNext <= iAddress)
			{
				break;	// wrapped around the end of the address space
			}
			iAddress = iNext;
		}
		return kSpace;
	}

	// C runtime heap (msvcr71, shared by the DLL, the exe and Python): bytes and blocks in use
	void scanCrtHeap(DWORDLONG& iUsed, int& iBlocks)
	{
		_HEAPINFO kInfo;
		kInfo._pentry = NULL;
		iUsed = 0;
		iBlocks = 0;
		while (_heapwalk(&kInfo) == _HEAPOK)
		{
			if (kInfo._useflag == _USEDENTRY)
			{
				iUsed += kInfo._size;
				iBlocks++;
			}
		}
	}

	int toMB(DWORDLONG iBytes)
	{
		return (int)(iBytes / (1024 * 1024));
	}
}

void VabiMemoryLogTurnStart()
{
	if (GC.getDefineINT("VABI_MEMORY_LOG") == 0)
	{
		return;
	}
	if (!s_bInit)
	{
		init();
		gDLL->logMsg("VabiMemory.log", "turn  year |  addr used / total MB  largest free MB | private MB  work set MB  peak ws MB | committed MB: private  mapped  image | crt heap MB (blocks) | page faults | read MB (ops)  written MB (ops) | units cities groups plotgrps  msgs  replay events | py objects", false, true);
		gDLL->logMsg("VabiMemory.log", "(page faults and read/written are since the previous line; committed private = DLL, Python, engine and driver allocations; crt heap = malloc/new of the DLL, exe and Python; py objects = objects tracked by the Python garbage collector)", false, true);
	}

	int iUsedMB = -1;
	int iTotalMB = -1;
	SIZE_T iLimit = 0x7FFF0000;
	if (s_pGlobalMemoryStatusEx != NULL)
	{
		VabiMemoryStatusEx kStatus;
		kStatus.dwLength = sizeof(kStatus);
		if (s_pGlobalMemoryStatusEx(&kStatus))
		{
			iTotalMB = toMB(kStatus.ullTotalVirtual);
			iUsedMB = toMB(kStatus.ullTotalVirtual - kStatus.ullAvailVirtual);
			iLimit = (SIZE_T)kStatus.ullTotalVirtual;
		}
	}
	VabiAddressSpace kSpace = scanAddressSpace(iLimit);

	DWORDLONG iCrtHeapUsed = 0;
	int iCrtHeapBlocks = 0;
	scanCrtHeap(iCrtHeapUsed, iCrtHeapBlocks);

	int iPrivateMB = -1;
	int iWorkingSetMB = -1;
	int iPeakWorkingSetMB = -1;
	int iPageFaults = -1;
	if (s_pGetProcessMemoryInfo != NULL)
	{
		VabiProcessMemoryCounters kMem;
		kMem.cb = sizeof(kMem);
		if (s_pGetProcessMemoryInfo(GetCurrentProcess(), &kMem, sizeof(kMem)))
		{
			iPrivateMB = toMB(kMem.PagefileUsage);
			iWorkingSetMB = toMB(kMem.WorkingSetSize);
			iPeakWorkingSetMB = toMB(kMem.PeakWorkingSetSize);
			iPageFaults = s_bHaveLast ? (int)(kMem.PageFaultCount - s_iLastPageFaults) : 0;
			s_iLastPageFaults = kMem.PageFaultCount;
		}
	}

	int iReadMB = -1, iReadOps = -1, iWriteMB = -1, iWriteOps = -1;
	if (s_pGetProcessIoCounters != NULL)
	{
		VabiIoCounters kIo;
		if (s_pGetProcessIoCounters(GetCurrentProcess(), &kIo))
		{
			if (s_bHaveLast)
			{
				iReadMB = toMB(kIo.ReadTransferCount - s_kLastIo.ReadTransferCount);
				iReadOps = (int)(kIo.ReadOperationCount - s_kLastIo.ReadOperationCount);
				iWriteMB = toMB(kIo.WriteTransferCount - s_kLastIo.WriteTransferCount);
				iWriteOps = (int)(kIo.WriteOperationCount - s_kLastIo.WriteOperationCount);
			}
			else
			{
				iReadMB = iReadOps = iWriteMB = iWriteOps = 0;
			}
			s_kLastIo = kIo;
		}
	}
	s_bHaveLast = true;

	// game objects: if memory grows while these stay flat, the growth is not game data
	int iUnits = 0, iCities = 0, iGroups = 0, iPlotGroups = 0, iMessages = 0, iEvents = 0;
	for (int iI = 0; iI < MAX_PLAYERS; iI++)
	{
		const CvPlayer& kPlayer = GET_PLAYER((PlayerTypes)iI);
		if (!kPlayer.isEverAlive())
		{
			continue;
		}
		iUnits += kPlayer.getNumUnits();
		iCities += kPlayer.getNumCities();
		iGroups += kPlayer.getNumSelectionGroups();
		iPlotGroups += kPlayer.getNumPlotGroups();
		iMessages += (int)kPlayer.getGameMessages().size();
		int iLoop;
		for (EventTriggeredData* pData = kPlayer.firstEventTriggered(&iLoop); pData != NULL; pData = kPlayer.nextEventTriggered(&iLoop))
		{
			iEvents++;
		}
	}
	int iReplayMessages = (int)GC.getGameINLINE().getNumReplayMessages();

	long lPythonObjects = -1;
	gDLL->getPythonIFace()->callFunction(PYCivModule, "vabiPythonObjectCount", NULL, &lPythonObjects);

	char szBuf[640];
	sprintf(szBuf, "%4d %5d | %9d / %5d MB  %15d | %10d  %11d  %10d | %20d  %6d  %5d | %6d (%8d) | %11d | %7d (%5d)  %10d (%5d) | %5d %6d %6d %8d %5d %7d %6d | %10ld",
		GC.getGameINLINE().getGameTurn(), GC.getGameINLINE().getGameTurnYear(),
		iUsedMB, iTotalMB, toMB(kSpace.iLargestFree),
		iPrivateMB, iWorkingSetMB, iPeakWorkingSetMB,
		toMB(kSpace.iCommittedPrivate), toMB(kSpace.iCommittedMapped), toMB(kSpace.iCommittedImage),
		toMB(iCrtHeapUsed), iCrtHeapBlocks,
		iPageFaults, iReadMB, iReadOps, iWriteMB, iWriteOps,
		iUnits, iCities, iGroups, iPlotGroups, iMessages, iReplayMessages, iEvents,
		lPythonObjects);
	gDLL->logMsg("VabiMemory.log", szBuf, false, true);
}
