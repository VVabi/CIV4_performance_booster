// ---------------------------------------------------------------------------------------------------
// FProfiler - DLL wrapper to FireEngine/FProfiler.cpp
//
// Author: tomw
//---------------------------------------------------------------------------------------------------------------------

#ifndef	__PROFILE_H__
#define __PROFILE_H__
#pragma once


#include "CvDLLEntityIFaceBase.h"
#include "CvDLLUtilityIFaceBase.h"
#include "CvGlobals.h"	// for gDLL

#ifndef FINAL_RELEASE
#ifndef FP_PROFILE_ENABLE 
	#define FP_PROFILE_ENABLE 
#endif
#endif

//NOTE: This struct must be identical ot the same struct in  FireEngine/FProfiler.h
//---------------------------------------------------------------------------------------------------------------------
struct ProfileSample
{
	ProfileSample(char *name)					{ strcpy(Name, name); Added=false; Parent=-1; }

	char	Name[256];						// Name of sample;

	unsigned int	ProfileInstances;		// # of times ProfileBegin Called
	int				OpenProfiles;			// # of time ProfileBegin called w/o ProfileEnd
	double			StartTime;				// The current open profile start time
	double			Accumulator;			// All samples this frame added together

	double			ChildrenSampleTime;		// Time taken by all children
	unsigned int	NumParents;				// Number of profile Parents
	bool			Added;					// true when added to the list
	int				Parent;
};

//---------------------------------------------------------------------------------------------------------------------
// Allows us to Profile based on Scope, to limit intrusion into code.
// Simply use PROFLE("funcname") instead having to insert begin()/end() pairing
class CProfileScope
{
public:
	CProfileScope() { bValid= false;};
	CProfileScope(ProfileSample *pSample)
	{
		m_pSample = pSample;
		bValid = true;
		gDLL->BeginSample(m_pSample);
	};
	~CProfileScope()
	{
		if(bValid )
		{
			gDLL->EndSample(m_pSample);
			bValid = false;
		}	
	};

private:
	bool bValid;
	ProfileSample *m_pSample;
};

//---------------------------------------------------------------------------------------------------------------------

// Timing profiler (build.bat Timing). Writes Logs\PerfProfile.log after each inter-turn / round.
#ifdef PERF_PROFILE
struct PerfProfSample
{
	PerfProfSample(const char* szName);
	const char* m_szName;
	__int64 m_iTotal;		// inclusive time (outermost call only when recursive)
	__int64 m_iSelf;		// exclusive time
	unsigned int m_iCalls;
	int m_iDepth;
	PerfProfSample* m_pNext;
	// sums over the current AI auto-play run (added up from the per-round values)
	__int64 m_iRunTotal;
	__int64 m_iRunSelf;
	unsigned int m_iRunCalls;
};

class PerfProfScope
{
public:
	PerfProfScope(PerfProfSample* pSample) : m_pSample(pSample), m_iChild(0), m_bOpen(true)
	{
		m_pParent = s_pCurrent;
		s_pCurrent = this;
		pSample->m_iCalls++;
		pSample->m_iDepth++;
		QueryPerformanceCounter((LARGE_INTEGER*)&m_iStart);
	}
	~PerfProfScope() { end(); }
	void end()
	{
		if (!m_bOpen) return;
		m_bOpen = false;
		__int64 iNow;
		QueryPerformanceCounter((LARGE_INTEGER*)&iNow);
		__int64 iElapsed = iNow - m_iStart;
		m_pSample->m_iSelf += iElapsed - m_iChild;
		if (--m_pSample->m_iDepth == 0)
		{
			m_pSample->m_iTotal += iElapsed;
		}
		if (m_pParent != NULL)
		{
			m_pParent->m_iChild += iElapsed;
		}
		s_pCurrent = m_pParent;
	}
	PerfProfSample* getSample() const { return m_pSample; }
	static PerfProfScope* s_pCurrent;
private:
	PerfProfSample* m_pSample;
	PerfProfScope* m_pParent;
	__int64 m_iStart;
	__int64 m_iChild;
	bool m_bOpen;
};

void PerfProfOnActiveTurnEnd(bool bHuman);	// called for the active player's civilization (PerfProfiler.cpp)
void PerfProfOnActiveTurnStart();
void PerfProfOnAutoPlayStart();				// called by CvGame::setAIAutoPlay
void PerfProfOnAutoPlayEnd(int iStartTurn, int iEndTurn, bool bStoppedEarly);
PerfProfSample* PerfProfCallerSample(const char* szPrefix);	// a sample per calling scope: "<prefix><caller>"
PerfProfSample* PerfProfNamedSample(const char* szPrefix, const char* szName);	// a sample per runtime name

#define PROFILE(name)\
	static PerfProfSample sample(name);\
	PerfProfScope ProfileScope(&sample);
#define PROFILE_BEGIN(name)\
	static PerfProfSample sample__(name);\
	PerfProfScope scope__(&sample__);
#define PROFILE_END()\
	scope__.end();
#define PROFILE_FUNC()\
	static PerfProfSample sample(__FUNCTION__);\
	PerfProfScope ProfileScope(&sample);

#else // PERF_PROFILE

// Main Interface for Profile
#ifdef FP_PROFILE_ENABLE				// Turn Profiling On or Off .. 
#define PROFILE(name)\
	static ProfileSample sample(name);\
	CProfileScope ProfileScope(&sample);		

//BEGIN & END macros:		Only needed if you don't want to use the scope macro above. 
// Macros must be in the same scope
#define PROFILE_BEGIN(name)\
	static ProfileSample sample__(name);\
	gDLL->BeginSample(&sample__);
#define PROFILE_END()\
	gDLL->EndSample(&sample__);

#define PROFILE_FUNC()\
	static ProfileSample sample(__FUNCTION__);\
	CProfileScope ProfileScope(&sample);		

#else
#define PROFILE(name)				// Remove profiling code
#define PROFILE_BEGIN(name)
#define PROFILE_END()
#define PROFILE_FUNC()
#endif
#endif // PERF_PROFILE


#endif //__PROFILE_H__


