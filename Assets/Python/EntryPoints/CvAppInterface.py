# Sid Meier's Civilization 4
# Copyright Firaxis Games 2005
#
# CvAppInterface.py
#
# These functions are App Entry Points from C++
# WARNING: These function names should not be changed
# WARNING: These functions can not be placed into a class
#
# No other modules should import this
#
# DONT ADD ANY MORE IMPORTS HERE - Moose
import sys
import os
import CvUtil
#

from CvPythonExtensions import *

# globals
gc = CyGlobalContext()

# don't make this an event - Moose
def init():
	# for PythonExtensions Help File
	PythonHelp = 0		# doesn't work on systems which haven't installed Python
			
	# dump Civ python module directory
	if PythonHelp:		
		import CvPythonExtensions
		helpFile=file("CvPythonExtensions.hlp.txt", "w")
		sys.stdout=helpFile
		import pydoc                  
		pydoc.help(CvPythonExtensions)
		helpFile.close()
	
	sys.stderr=CvUtil.RedirectError()
	sys.excepthook = CvUtil.myExceptHook
	sys.stdout=CvUtil.RedirectDebug()

def onSave():
	'Here is your chance to save data.  This function should return a string'
	import CvWBDesc
	import pickle	
	import CvEventInterface
	# if the tutorial is active, it will save out the Shown Messages list
	saveDataStr = pickle.dumps( CvEventInterface.onEvent( ('OnSave',0,0,0,0,0 ) ) )
	return saveDataStr
	
def onLoad(argsList):
	'Called when a file is loaded'
	import pickle	
	import CvEventInterface
	loadDataStr=argsList[0]	
	if len(loadDataStr):
		CvEventInterface.onEvent( ('OnLoad',pickle.loads(loadDataStr),0,0,0,0,0 ) )	

def preGameStart():
	import CvScreensInterface
	
	if not CyGame().isPitbossHost():
		NiTextOut("Initializing font icons")
		# Load dynamic font icons into the icon map
		CvUtil.initDynamicFontIcons()

	if not CyGame().isPitbossHost():
		# Preload the tech chooser..., only do this release builds, in debug build we may not be raising the tech chooser
		if (not gc.isDebugBuild()):
			NiTextOut("Preloading tech chooser")
			CvScreensInterface.showTechChooser()
			CvScreensInterface.techChooser.hideScreen()
		
	NiTextOut("Loading main interface...")
	CvScreensInterface.showMainInterface()	

def onPbemSend(argsList):
	import sys, smtplib, MimeWriter, base64, StringIO
			
	szToAddr = argsList[0]	
	szFromAddr = argsList[1]	
	szSubject = argsList[2]	
	szPath = argsList[3]
	szFilename = argsList[4]
	szHost = argsList[5]
	szUser = argsList[6]
	szPassword = argsList[7]
	
	print 'sending e-mail'
	print 'To:', szToAddr
	print 'From:', szFromAddr
	print 'Subject:', szSubject
	print 'Path:', szPath
	print 'File:', szFilename
	print 'Server:', szHost
	print 'User:', szUser
	
	if len(szFromAddr) == 0 or len(szHost) == 0:
		print 'host or address empty'
		return 1

	message = StringIO.StringIO()
	writer = MimeWriter.MimeWriter(message)

	writer.addheader('To', szToAddr)
	writer.addheader('From', szFromAddr)
	writer.addheader('Subject', szSubject)
	writer.addheader('MIME-Version', '1.0')
	writer.startmultipartbody('mixed')

	part = writer.nextpart()
	body = part.startbody('text/plain')
	body.write('CIV4 PBEM save attached')

	part = writer.nextpart()
	part.addheader('Content-Transfer-Encoding', 'base64')
	szStartBody = "application/CivBeyondSwordSave; name=%s" % szFilename
	body = part.startbody(szStartBody)
	base64.encode(open(szPath+szFilename, 'rb'), body)

	# finish off
	writer.lastpart()

	# send the mail
	try:
		smtp = smtplib.SMTP(szHost)
		# trying to get TLS to work...
		#smtp.set_debuglevel(1)
		#smtp.ehlo()
		#smtp.starttls()
		#smtp.ehlo()
		if len(szUser) > 0:
			smtp.login(szUser, szPassword)
		smtp.sendmail(szFromAddr, szToAddr, message.getvalue())
		smtp.quit()
	except smtplib.SMTPAuthenticationError, e:
		CyInterface().addImmediateMessage("Authentication Error: The server didn't accept the username/password combination provided.", "")	
		CyInterface().addImmediateMessage("Error %d: %s" % (e.smtp_code, e.smtp_error), "")	
		return 1
	except smtplib.SMTPHeloError, e:
		CyInterface().addImmediateMessage("The server refused our HELO reply.", "")	
		CyInterface().addImmediateMessage("Error %d: %s" % (e.smtp_code, e.smtp_error), "")	
		return 1
	except smtplib.SMTPConnectError, e:
		CyInterface().addImmediateMessage("Error establishing connection.", "")	
		CyInterface().addImmediateMessage("Error %d: %s" % (e.smtp_code, e.smtp_error), "")	
		return 1
	except smtplib.SMTPDataError, e:
		CyInterface().addImmediateMessage("The SMTP server didn't accept the data.", "")	
		CyInterface().addImmediateMessage("Error %d: %s" % (e.smtp_code, e.smtp_error), "")	
		return 1
	except smtplib.SMTPRecipientsRefused, e:
		CyInterface().addImmediateMessage("All recipient addresses refused.", "")	
		return 1
	except smtplib.SMTPSenderRefused, e:
		CyInterface().addImmediateMessage("Sender address refused.", "")	
		CyInterface().addImmediateMessage("Error %d: %s" % (e.smtp_code, e.smtp_error), "")	
		return 1
	except smtplib.SMTPResponseException, e:
		CyInterface().addImmediateMessage("Error %d: %s" % (e.smtp_code, e.smtp_error), "")	
		return 1
	except smtplib.SMTPServerDisconnected:
		CyInterface().addImmediateMessage("Not connected to any SMTP server", "")	
		return 1
	except:
		return 1
	return 0

#####################################33
## INTERNAL USE ONLY
#####################################33
def normalizePath(argsList):
	CvUtil.pyPrint("PathName in = %s" %(argsList[0],))
	pathOut=os.path.normpath(argsList[0])
	CvUtil.pyPrint("PathName out = %s" %(pathOut,))
	return pathOut

def getConsoleMacro(argsList):
	'return a string macro that is used by the in-game python console, fxnKey goes from 1 to 10'
	fxnKey = argsList[0]
	if (fxnKey==1): return "player = gc.getPlayer(0)"
	if (fxnKey==2): return "import CvCameraControls"
	if (fxnKey==3): return "CvCameraControls.g_CameraControls.resetCameraControls()"
	if (fxnKey==4): return "CvCameraControls.g_CameraControls.doRotateCamera(360, 45.0)"
	if (fxnKey==5): return "CvCameraControls.g_CameraControls.doZoomCamera(0.2, 0.5)"
	if (fxnKey==6): return "CvCameraControls.g_CameraControls.doZoomCamera(0.5, 0.15)"
	if (fxnKey==7): return "CvCameraControls.g_CameraControls.doPitchCamera(0.5, 0.5)"
	return ""
def vabiPythonObjectCount():
	'number of objects tracked by the Python garbage collector, for Logs\VabiMemory.log (called from the DLL)'
	import gc
	return len(gc.get_objects())

# Performance: which CvGameUtils callbacks always return the same constant (checked once per session by the DLL,
# see VabiPythonCallbacks.cpp). A callback qualifies only if the CvGameInterface function is exactly
# "return gameUtils().<name>(argsList)" and the CvGameUtils method only unpacks its arguments and returns a
# constant (no calls, no global or attribute access except True/False/None). Anything else is always called.
# Return value for the DLL: 0 = must be called, 2*v+1 = always returns the integer (or bool) v.
def _vabiOpcodes():
	try:
		import opcode
		return opcode.HAVE_ARGUMENT, opcode.opmap
	except:
		return 90, {'POP_TOP': 1, 'BINARY_SUBSCR': 25, 'RETURN_VALUE': 83, 'UNPACK_SEQUENCE': 92, 'LOAD_CONST': 100, 'LOAD_ATTR': 105, 'LOAD_GLOBAL': 116, 'LOAD_FAST': 124, 'STORE_FAST': 125, 'CALL_FUNCTION': 131}

def _vabiReachableOps(code, iHaveArgument):
	'opcodes up to and including the first RETURN_VALUE (without jumps, the rest is unreachable)'
	co = code.co_code
	ops = []
	i = 0
	while i < len(co):
		op = ord(co[i])
		if op >= iHaveArgument:
			arg = ord(co[i + 1]) + 256 * ord(co[i + 2])
			i += 3
		else:
			arg = None
			i += 1
		ops.append((op, arg))
		if op == 83:
			break
	return ops

def vabiConstantCallback(argsList):
	try:
		szName = argsList[0]
		import CvGameInterface
		iHaveArgument, m = _vabiOpcodes()
		# the interface function must only forward to gameUtils()
		fWrapper = getattr(CvGameInterface, szName, None)
		if fWrapper is None or not hasattr(fWrapper, 'func_code'):
			return 0
		wc = fWrapper.func_code
		ops = _vabiReachableOps(wc, iHaveArgument)
		if wc.co_argcount != 1 or len(ops) != 6:
			return 0
		expected = [m['LOAD_GLOBAL'], m['CALL_FUNCTION'], m['LOAD_ATTR'], m['LOAD_FAST'], m['CALL_FUNCTION'], m['RETURN_VALUE']]
		if [op for (op, arg) in ops] != expected:
			return 0
		if wc.co_names[ops[0][1]] != 'gameUtils' or ops[1][1] != 0 or wc.co_names[ops[2][1]] != szName or ops[3][1] != 0 or ops[4][1] != 1:
			return 0
		# the game utils method must only unpack its arguments and return a constant
		method = getattr(CvGameInterface.gameUtils(), szName, None)
		if method is None or not hasattr(method, 'im_func'):
			return 0
		code = method.im_func.func_code
		if code.co_argcount != 2 or (code.co_flags & 0x2C) or code.co_freevars or code.co_cellvars:
			return 0
		ops = _vabiReachableOps(code, iHaveArgument)
		if len(ops) < 2 or ops[-1][0] != m['RETURN_VALUE']:
			return 0
		tail = ops[:-1]
		# "return -1": Python 2.4 compiles negative constants as LOAD_CONST 1, UNARY_NEGATIVE
		bNegate = (tail[-1][0] == m.get('UNARY_NEGATIVE', 11))
		if bNegate:
			tail = tail[:-1]
		if len(tail) < 1:
			return 0
		allowed = [m['LOAD_FAST'], m['STORE_FAST'], m['LOAD_CONST'], m['BINARY_SUBSCR'], m['UNPACK_SEQUENCE'], m['POP_TOP'], m['LOAD_GLOBAL']]
		for (op, arg) in tail:
			if op not in allowed:
				return 0
			if op == m['LOAD_GLOBAL'] and code.co_names[arg] not in ('True', 'False', 'None'):
				return 0
		(op, arg) = tail[-1]
		if op == m['LOAD_CONST']:
			value = code.co_consts[arg]
		elif op == m['LOAD_GLOBAL']:
			value = {'True': True, 'False': False, 'None': None}[code.co_names[arg]]
		else:
			return 0
		if value is None or type(value) not in (type(0), type(True)):
			return 0
		value = int(value)
		if bNegate:
			value = -value
		if value < -100000000 or value > 100000000:
			return 0
		return 2 * value + 1
	except:
		return 0
