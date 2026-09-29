// random.cpp

#include "CvGameCoreDLL.h"
#include "CvRandom.h"
#include "CvGlobals.h"
#include "CyArgsList.h"
#include "CvGameAI.h"

#define RANDOM_A      (1103515245)
#define RANDOM_C      (12345)
#define RANDOM_SHIFT  (16)

CvRandom::CvRandom()
{ 
	reset();
}


CvRandom::~CvRandom()
{ 
	uninit();
}


void CvRandom::init(unsigned long ulSeed)
{
	//--------------------------------
	// Init saved data
	reset(ulSeed);

	//--------------------------------
	// Init non-saved data
}


void CvRandom::uninit()
{
}


// FUNCTION: reset()
// Initializes data members that are serialized.
void CvRandom::reset(unsigned long ulSeed)
{
	//--------------------------------
	// Uninit class
	uninit();

	m_ulRandomSeed = ulSeed;
}


unsigned short CvRandom::get(unsigned short usNum, const TCHAR* pszLog)
{
	// testing aid: with AUTOPLAY_RAND_LOG_TURN > 0, every game random number drawn during that game turn of an
	// AI auto-play run is written to AutoPlayRand.log (reason and seed), so two runs can be compared draw by draw
	static int s_iRandLogTurn = -1;
	if (s_iRandLogTurn < 0)
	{
		s_iRandLogTurn = std::max(0, GC.getDefineINT("AUTOPLAY_RAND_LOG_TURN"));
	}
	if (s_iRandLogTurn > 0 && this == &(GC.getGameINLINE().getSorenRand()) &&
		GC.getGameINLINE().getGameTurn() == s_iRandLogTurn && GC.getGameINLINE().getAIAutoPlay() > 0)
	{
		CvString szLine;
		szLine.Format("seed %u, range %u: %s", m_ulRandomSeed, (unsigned int)usNum, (pszLog != NULL) ? pszLog : "(no reason)");
		gDLL->logMsg("AutoPlayRand.log", szLine.c_str(), false, false);
	}

	if (pszLog != NULL)
	{
		if (GC.getLogging() && GC.getRandLogging())
		{
			if (GC.getGameINLINE().getTurnSlice() > 0)
			{
				TCHAR szOut[1024];
				sprintf(szOut, "Rand = %d on %d (%s)\n", getSeed(), GC.getGameINLINE().getTurnSlice(), pszLog);
				gDLL->messageControlLog(szOut);
			}
		}
	}

	m_ulRandomSeed = ((RANDOM_A * m_ulRandomSeed) + RANDOM_C);

	unsigned short us = ((unsigned short)((((m_ulRandomSeed >> RANDOM_SHIFT) & MAX_UNSIGNED_SHORT) * ((unsigned long)usNum)) / (MAX_UNSIGNED_SHORT + 1)));

	return us;
}


float CvRandom::getFloat()
{
	return (((float)(get(MAX_UNSIGNED_SHORT))) / ((float)MAX_UNSIGNED_SHORT));
}


void CvRandom::reseed(unsigned long ulNewValue)
{
	m_ulRandomSeed = ulNewValue;
}


unsigned long CvRandom::getSeed()
{
	return m_ulRandomSeed;
}


void CvRandom::read(FDataStreamBase* pStream)
{
	reset();

	pStream->Read(&m_ulRandomSeed);
}


void CvRandom::write(FDataStreamBase* pStream)
{
	pStream->Write(m_ulRandomSeed);
}
