// Timing profiler (ported from VabiGEM) - only active in the "Timing" build (build.bat Timing, defines VABI_PROFILE).
// Measures every PROFILE / PROFILE_FUNC scope with QueryPerformanceCounter and writes Logs\VabiProfile.log
// whenever the active player's turn starts:
// - normal play: the counters are reset when the human ends his turn, so each report covers exactly one
//   inter-turn (all AI turns plus the game's doTurn), without the human's own thinking time;
// - AI auto-play (the AI plays the active player's civilization): nothing is reset at its turn end, so each
//   report covers one full round including that civilization's own AI turn.
#include "CvGameCoreDLL.h"
#include "FProfiler.h"

#ifdef VABI_PROFILE

#include <algorithm>

static VabiProfSample* s_pFirstSample = NULL;
VabiProfScope* VabiProfScope::s_pCurrent = NULL;
static __int64 s_iIntervalStart = 0;

VabiProfSample::VabiProfSample(const char* szName) :
	m_szName(szName), m_iTotal(0), m_iSelf(0), m_iCalls(0), m_iDepth(0)
{
	m_pNext = s_pFirstSample;
	s_pFirstSample = this;
}

static bool sortByTotal(const VabiProfSample* a, const VabiProfSample* b) { return a->m_iTotal > b->m_iTotal; }
static bool sortBySelf(const VabiProfSample* a, const VabiProfSample* b) { return a->m_iSelf > b->m_iSelf; }

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

static void writeTable(std::vector<VabiProfSample*>& aSamples, double fMsPerTick, int iMaxLines)
{
	char szBuf[512];
	sprintf(szBuf, "%-64s %12s %12s %12s %10s", "Function", "total ms", "self ms", "calls", "us/call");
	gDLL->logMsg("VabiProfile.log", szBuf, false, false);
	int iLines = 0;
	for (int i = 0; i < (int)aSamples.size() && iLines < iMaxLines; i++)
	{
		VabiProfSample* p = aSamples[i];
		if (p->m_iCalls == 0)
		{
			continue;
		}
		sprintf(szBuf, "%-64.64s %12.1f %12.1f %12u %10.2f", p->m_szName,
			p->m_iTotal * fMsPerTick, p->m_iSelf * fMsPerTick, p->m_iCalls,
			p->m_iTotal * fMsPerTick * 1000.0 / p->m_iCalls);
		gDLL->logMsg("VabiProfile.log", szBuf, false, false);
		iLines++;
	}
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
	__int64 iNow, iFreq;
	QueryPerformanceCounter((LARGE_INTEGER*)&iNow);
	QueryPerformanceFrequency((LARGE_INTEGER*)&iFreq);
	double fMsPerTick = 1000.0 / (double)iFreq;

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
	writeTable(aSamples, fMsPerTick, 60);

	gDLL->logMsg("VabiProfile.log", "--- Sorted by total time (including called functions) ---", false, false);
	std::sort(aSamples.begin(), aSamples.end(), sortByTotal);
	writeTable(aSamples, fMsPerTick, 80);

	startInterval();
}

#endif // VABI_PROFILE
