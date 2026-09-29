// Timing profiler (ported from VabiGEM) - only active in the "Timing" build (build.bat Timing, defines VABI_PROFILE).
// Measures every PROFILE / PROFILE_FUNC scope with QueryPerformanceCounter and writes Logs\VabiProfile.log
// whenever the active player's turn starts:
// - normal play: the counters are reset when the human ends his turn, so each report covers exactly one
//   inter-turn (all AI turns plus the game's doTurn), without the human's own thinking time;
// - AI auto-play (the AI plays the active player's civilization): nothing is reset at its turn end, so each
//   report covers one full round including that civilization's own AI turn. The rounds are also added up,
//   and a summary of the whole auto-play run is written when it ends.
#include "CvGameCoreDLL.h"
#include "FProfiler.h"

#ifdef VABI_PROFILE

#include <algorithm>
#include <map>

static VabiProfSample* s_pFirstSample = NULL;
VabiProfScope* VabiProfScope::s_pCurrent = NULL;
static __int64 s_iIntervalStart = 0;
static bool s_bRunActive = false;
static __int64 s_iRunStart = 0;

VabiProfSample::VabiProfSample(const char* szName) :
	m_szName(szName), m_iTotal(0), m_iSelf(0), m_iCalls(0), m_iDepth(0),
	m_iRunTotal(0), m_iRunSelf(0), m_iRunCalls(0)
{
	m_pNext = s_pFirstSample;
	s_pFirstSample = this;
}

static bool sortByTotal(const VabiProfSample* a, const VabiProfSample* b) { return a->m_iTotal > b->m_iTotal; }
static bool sortBySelf(const VabiProfSample* a, const VabiProfSample* b) { return a->m_iSelf > b->m_iSelf; }
static bool sortByRunTotal(const VabiProfSample* a, const VabiProfSample* b) { return a->m_iRunTotal > b->m_iRunTotal; }
static bool sortByRunSelf(const VabiProfSample* a, const VabiProfSample* b) { return a->m_iRunSelf > b->m_iRunSelf; }

static double msPerTick()
{
	__int64 iFreq;
	QueryPerformanceFrequency((LARGE_INTEGER*)&iFreq);
	return 1000.0 / (double)iFreq;
}

static void startInterval()
{
	for (VabiProfSample* p = s_pFirstSample; p != NULL; p = p->m_pNext)
	{
		p->m_iTotal = 0;
		p->m_iSelf = 0;
		p->m_iCalls = 0;
	}
	QueryPerformanceCounter((LARGE_INTEGER*)&s_iIntervalStart);
}

// adds the current interval to the auto-play run (before the interval counters are reset)
static void addIntervalToRun()
{
	if (!s_bRunActive)
	{
		return;
	}
	for (VabiProfSample* p = s_pFirstSample; p != NULL; p = p->m_pNext)
	{
		p->m_iRunTotal += p->m_iTotal;
		p->m_iRunSelf += p->m_iSelf;
		p->m_iRunCalls += p->m_iCalls;
	}
}

static void writeTable(std::vector<VabiProfSample*>& aSamples, double fMsPerTick, int iMaxLines, bool bRun)
{
	char szBuf[512];
	sprintf(szBuf, "%-64s %12s %12s %12s %10s", "Function", "total ms", "self ms", "calls", "us/call");
	gDLL->logMsg("VabiProfile.log", szBuf, false, false);
	int iLines = 0;
	for (int i = 0; i < (int)aSamples.size() && iLines < iMaxLines; i++)
	{
		VabiProfSample* p = aSamples[i];
		__int64 iTotal = bRun ? p->m_iRunTotal : p->m_iTotal;
		__int64 iSelf = bRun ? p->m_iRunSelf : p->m_iSelf;
		unsigned int iCalls = bRun ? p->m_iRunCalls : p->m_iCalls;
		if (iCalls == 0)
		{
			continue;
		}
		sprintf(szBuf, "%-64.64s %12.1f %12.1f %12u %10.2f", p->m_szName,
			iTotal * fMsPerTick, iSelf * fMsPerTick, iCalls, iTotal * fMsPerTick * 1000.0 / iCalls);
		gDLL->logMsg("VabiProfile.log", szBuf, false, false);
		iLines++;
	}
}

// one sample per (prefix, caller) pair, e.g. to see which AI functions request the path searches
VabiProfSample* VabiProfCallerSample(const char* szPrefix)
{
	static std::map<std::pair<const char*, VabiProfSample*>, VabiProfSample*> s_mapSamples;
	VabiProfSample* pCaller = (VabiProfScope::s_pCurrent != NULL) ? VabiProfScope::s_pCurrent->getSample() : NULL;
	std::pair<const char*, VabiProfSample*> key(szPrefix, pCaller);
	std::map<std::pair<const char*, VabiProfSample*>, VabiProfSample*>::iterator it = s_mapSamples.find(key);
	if (it != s_mapSamples.end())
	{
		return it->second;
	}
	const char* szCaller = (pCaller != NULL) ? pCaller->m_szName : "(no caller)";
	char* szName = new char[strlen(szPrefix) + strlen(szCaller) + 1];	// kept for the whole session
	strcpy(szName, szPrefix);
	strcat(szName, szCaller);
	VabiProfSample* pSample = new VabiProfSample(szName);
	s_mapSamples[key] = pSample;
	return pSample;
}

void VabiProfOnActiveTurnEnd(bool bHuman)
{
	if (bHuman)
	{
		startInterval(); // do not count the time the human spends on his own turn
	}
}

void VabiProfOnActiveTurnStart()
{
	if (s_iIntervalStart == 0)
	{
		startInterval(); // first turn after loading: no complete interval measured yet
		return;
	}
	__int64 iNow;
	QueryPerformanceCounter((LARGE_INTEGER*)&iNow);
	double fMsPerTick = msPerTick();

	std::vector<VabiProfSample*> aSamples;
	for (VabiProfSample* p = s_pFirstSample; p != NULL; p = p->m_pNext)
	{
		aSamples.push_back(p);
	}

	char szBuf[512];
	gDLL->logMsg("VabiProfile.log", "", false, false);
	sprintf(szBuf, "===== %s before game turn %d: %.1f s wall clock =====",
		(GC.getGameINLINE().getAIAutoPlay() > 0) ? "AI auto-play round" : "Inter-turn",
		GC.getGameINLINE().getGameTurn(), (iNow - s_iIntervalStart) * fMsPerTick / 1000.0);
	gDLL->logMsg("VabiProfile.log", szBuf, false, false);

	__int64 iInstrumented = 0;
	for (int i = 0; i < (int)aSamples.size(); i++)
	{
		iInstrumented += aSamples[i]->m_iSelf;
	}
	sprintf(szBuf, "Time inside profiled DLL code: %.1f s (the rest is the exe: graphics, animations, interface, ...)",
		iInstrumented * fMsPerTick / 1000.0);
	gDLL->logMsg("VabiProfile.log", szBuf, false, false);

	gDLL->logMsg("VabiProfile.log", "--- Sorted by self time (time spent in the function itself) ---", false, false);
	std::sort(aSamples.begin(), aSamples.end(), sortBySelf);
	writeTable(aSamples, fMsPerTick, 60, false);

	gDLL->logMsg("VabiProfile.log", "--- Sorted by total time (including called functions) ---", false, false);
	std::sort(aSamples.begin(), aSamples.end(), sortByTotal);
	writeTable(aSamples, fMsPerTick, 80, false);

	addIntervalToRun();
	startInterval();
}

void VabiProfOnAutoPlayStart()
{
	for (VabiProfSample* p = s_pFirstSample; p != NULL; p = p->m_pNext)
	{
		p->m_iRunTotal = 0;
		p->m_iRunSelf = 0;
		p->m_iRunCalls = 0;
	}
	s_bRunActive = true;
	startInterval(); // the run starts now; the human's partial turn before it is not counted
	s_iRunStart = s_iIntervalStart;
}

void VabiProfOnAutoPlayEnd(int iStartTurn, int iEndTurn, bool bStoppedEarly)
{
	if (!s_bRunActive)
	{
		return; // auto-play was running when the game was loaded: no start time
	}
	addIntervalToRun();	// the last, possibly partial round
	s_bRunActive = false;

	__int64 iNow;
	QueryPerformanceCounter((LARGE_INTEGER*)&iNow);
	double fMsPerTick = msPerTick();
	double fSeconds = (iNow - s_iRunStart) * fMsPerTick / 1000.0;
	int iRounds = std::max(1, iEndTurn - iStartTurn);

	std::vector<VabiProfSample*> aSamples;
	__int64 iInstrumented = 0;
	for (VabiProfSample* p = s_pFirstSample; p != NULL; p = p->m_pNext)
	{
		aSamples.push_back(p);
		iInstrumented += p->m_iRunSelf;
	}

	char szBuf[512];
	gDLL->logMsg("VabiProfile.log", "", false, false);
	sprintf(szBuf, "##### AI auto-play run summary: game turns %d - %d (%d rounds%s): %.1f s wall clock, %.2f s per round #####",
		iStartTurn, iEndTurn, iEndTurn - iStartTurn, bStoppedEarly ? ", stopped early" : "", fSeconds, fSeconds / iRounds);
	gDLL->logMsg("VabiProfile.log", szBuf, false, false);
	sprintf(szBuf, "Time inside profiled DLL code: %.1f s (%.2f s per round)",
		iInstrumented * fMsPerTick / 1000.0, iInstrumented * fMsPerTick / 1000.0 / iRounds);
	gDLL->logMsg("VabiProfile.log", szBuf, false, false);

	gDLL->logMsg("VabiProfile.log", "--- Whole run, sorted by self time ---", false, false);
	std::sort(aSamples.begin(), aSamples.end(), sortByRunSelf);
	writeTable(aSamples, fMsPerTick, 60, true);

	gDLL->logMsg("VabiProfile.log", "--- Whole run, sorted by total time ---", false, false);
	std::sort(aSamples.begin(), aSamples.end(), sortByRunTotal);
	writeTable(aSamples, fMsPerTick, 80, true);

	std::vector<VabiProfSample*> aCallers;
	for (int i = 0; i < (int)aSamples.size(); i++)
	{
		if (strstr(aSamples[i]->m_szName, " <- ") != NULL)
		{
			aCallers.push_back(aSamples[i]);
		}
	}
	if (!aCallers.empty())
	{
		gDLL->logMsg("VabiProfile.log", "--- Whole run, calls by calling function (\"x <- caller\"; total = time spent in x) ---", false, false);
		writeTable(aCallers, fMsPerTick, 1000, true);
	}
}

#endif // VABI_PROFILE
