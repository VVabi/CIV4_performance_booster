// Performance: skipping Python callbacks that always return the same constant.
// The DLL calls many CvGameUtils callbacks (via CvGameInterface) for every unit update, city turn etc. In the
// unmodified BtS Python most of them only return False. perfConstantCallback in CvAppInterface.py checks the
// Python bytecode once per session and callback name: if the callback can only ever return one constant (no
// calls, no access to the game), the DLL uses that constant instead of calling Python. Callbacks replaced by a
// mod are recognized as non-constant and called as before. Switch: PYTHON_SKIP_TRIVIAL_CALLBACKS
// (GlobalDefinesAlt.xml). The result per callback is written to Logs\PerfPythonCallbacks.log.
#include "CvGameCoreDLL.h"
#include "CyArgsList.h"
#include <map>
#include <string>

// returns true if the Python callback szName always returns the same integer; then *plResult is set to it and
// the caller does not need to call Python
bool perfConstantPythonCallback(const char* szName, long* plResult)
{
	static int s_iEnabled = -1;
	if (s_iEnabled < 0)
	{
		s_iEnabled = (GC.getDefineINT("PYTHON_SKIP_TRIVIAL_CALLBACKS") > 0) ? 1 : 0;
	}
	if (s_iEnabled == 0)
	{
		return false;
	}

	// encoded answer of perfConstantCallback: 0 = must be called, 2*v+1 = always returns v
	static std::map<std::string, long> s_mapCode;
	long lCode;
	std::map<std::string, long>::iterator it = s_mapCode.find(szName);
	if (it != s_mapCode.end())
	{
		lCode = it->second;
	}
	else
	{
		CyArgsList argsList;
		argsList.add(szName);
		lCode = 0;
		if (!gDLL->getPythonIFace()->callFunction(PYCivModule, "perfConstantCallback", argsList.makeFunctionArgs(), &lCode))
		{
			lCode = 0;
		}
		if (lCode != 0 && (lCode % 2) == 0)
		{
			lCode = 0;	// unexpected answer: call as usual
		}
		s_mapCode[szName] = lCode;

		CvString szLine;
		if (lCode == 0)
		{
			szLine.Format("%s: called (not a constant callback)", szName);
		}
		else
		{
			szLine.Format("%s: skipped, always returns %d", szName, (int)((lCode - 1) / 2));
		}
		gDLL->logMsg("PerfPythonCallbacks.log", szLine.c_str(), false, false);
	}

	if (lCode == 0)
	{
		return false;
	}
	*plResult = (lCode - 1) / 2;
	return true;
}
