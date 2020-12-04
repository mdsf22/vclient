// vclient.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "shadow.h"
#include <iostream>

#define ASR_WRITER_XML L"asr.xml"
#define METADATA_XML L"meta.xml"
#define VOLUMEPREFIX  L"\\\\?\\Volume"
#define GLOBALROOTPREFIX L"\\\\?\\GLOBALROOT\\Device"
#define HARDDISKVOLUME   L"HarddiskVolume"

CommandLineParser::CommandLineParser():
	m_efi_partition(L""), m_bPersistent(false), m_bWithWriters(true), m_bWaitForFinish(false)
{
	//// true if it is a is persistent snapshot
	//m_bPersistent = true;

	//// false if the snapshot creation is without writers
	//m_bWithWriters = true;

	//// true if the user wants to wait for termination
	//m_bWaitForFinish = false;
}



// Destructor
CommandLineParser::~CommandLineParser()
{
	FunctionTracer ft(DBG_INFO);

	if (m_bWaitForFinish)
	{
		ft.WriteLine(L"\nPress <ENTER> to continue...");

#pragma warning(suppress: 6031)  //Intentionally ignore the return value of getchar() 
		getchar();
	}
}

DWORD CommandLineParser::UpdateFinalContext(DWORD dwContext)
{
	if (m_bPersistent)
	{
		if (m_bWithWriters)
			dwContext |= VSS_CTX_APP_ROLLBACK;
		else
			dwContext |= VSS_CTX_NAS_ROLLBACK;
	}
	else
	{
		if (m_bWithWriters)
			dwContext |= VSS_CTX_BACKUP;
		else
			dwContext |= VSS_CTX_FILE_SHARE_BACKUP;
	}
	return dwContext;
}

bool CommandLineParser::MatchArgument(wstring argument, wstring optionPattern)
{
	FunctionTracer ft(DBG_INFO);

	ft.Trace(DBG_INFO, L"Matching Arg: '%s' with '%s'\n", argument.c_str(), optionPattern.c_str());

	bool retVal = (IsEqual(argument, wstring(L"/") + optionPattern) || IsEqual(argument, wstring(L"-") + optionPattern));

	ft.Trace(DBG_INFO, L"Return: %s\n", BOOL2TXT(retVal));
	return retVal;
}


// Returns TRUE if the argument is in the following formats
//  -xxxx=yyyy
//  /xxxx=yyyy
// where xxxx is the option pattern and yyyy the additional parameter (eventually enclosed in ' or ")
bool CommandLineParser::MatchArgument(wstring argument, wstring optionPattern, wstring & additionalParameter)
{
	FunctionTracer ft(DBG_INFO);

	ft.Trace(DBG_INFO, L"Matching Arg: '%s' with '%s'", argument.c_str(), optionPattern.c_str());

	_ASSERTE(argument.length() > 0);

	if ((argument[0] != L'/') && (argument[0] != L'-'))
		return false;

	// Find the '=' separator between the option and the additional parameter
	size_t equalSignPos = argument.find(L'=');
	if ((equalSignPos == wstring::npos) || (equalSignPos == 0))
		return false;

	ft.Trace(DBG_INFO, L"%s %d", argument.substr(1, equalSignPos - 1).c_str(), equalSignPos);

	// Check to see if this is our option
	if (!IsEqual(argument.substr(1, equalSignPos - 1), optionPattern))
		return false;

	// Isolate the additional parameter
	additionalParameter = argument.substr(equalSignPos + 1);

	ft.Trace(DBG_INFO, L"- Additional Param: [%s]", additionalParameter.c_str());

	// We cannot have an empty additional parameter
	if (additionalParameter.length() == 0)
		return false;

	// Eliminate the enclosing quotes, if any
	size_t lastPos = additionalParameter.length() - 1;
	if ((additionalParameter[0] == L'"') && (additionalParameter[lastPos] == L'"'))
		additionalParameter = additionalParameter.substr(1, additionalParameter.length() - 2);

	ft.Trace(DBG_INFO, L"Return true; (additional param = %s)", additionalParameter.c_str());

	return true;
}

int CommandLineParser::MainRoutine(vector<wstring> arguments)
{
	FunctionTracer ft(DBG_INFO);

	DWORD dwContext = VSS_CTX_BACKUP;
	vector<wstring> excludedWriterList;
	vector<wstring> includedWriterList;
	wstring xmlBackupComponentsDoc;
	wstring dir;
	vector<wstring> filelist;
	wstring files;
	vector<wstring> volumeList;

	// 1 backup 2 restore
	int opt = 0;

	for (unsigned argIndex = 0; argIndex < arguments.size(); argIndex++)
	{

		if (MatchArgument(arguments[argIndex], L"d", dir))
		{
			ft.WriteLine(L" dir of rawfile %s", dir.c_str());
			if (dir[dir.length() - 1] != L'\\')
				dir += L'\\';
			continue;
		}

		if (MatchArgument(arguments[argIndex], L"f", files))
		{
			ft.WriteLine(L" files %s", files.c_str());
			filelist = SplitWString(files, L',');
			continue;
		}

		if (MatchArgument(arguments[argIndex], L"efi", m_efi_partition))
		{
			ft.WriteLine(L" efi partition %s", m_efi_partition.c_str());
			continue;
		}

		// restore
		if (MatchArgument(arguments[argIndex], L"r", xmlBackupComponentsDoc))
		{
			opt = 2;
			continue;
			/*
			ft.WriteLine(L"(Option: Perform a restore)");
			wstring xmlDoc = ReadFileContents(xmlBackupComponentsDoc);
			ft.Trace(DBG_INFO, L"XML document: '%s'", xmlDoc.c_str());

			m_vssClient.Initialize(VSS_CTX_ALL, xmlDoc, true);
			m_vssClient.PreRestore();

			// copy vol
			Copy copy;
			wstring srcRaw, dstVol;
			for (vector<wstring>::iterator iter = filelist.begin(); iter != filelist.end(); ++iter)
			{
				srcRaw = dir + *iter;
				dstVol = VOLUMEPREFIX + *iter;

				ft.WriteLine(L"srcRaw %s ,dstVol %s", srcRaw.c_str(), dstVol.c_str());
				ft.WriteLine(L"\nPress Y to restore %s", dstVol.c_str());
				string flag;
				cin >> flag;
				if (flag == "y" || flag == "Y")
				{
					ft.WriteLine(L"restore vol %s", dstVol.c_str());
					copy.raw2vol(srcRaw, dstVol);
				}
			}

			// chkdsk
			wstring cmd_chkdsk;
			for (vector<wstring>::iterator iter = filelist.begin(); iter != filelist.end(); ++iter)
			{
				dstVol = VOLUMEPREFIX + *iter;
				cmd_chkdsk = L"chkdsk " + dstVol + L" /x";
				ft.WriteLine(L"cmd_chkdsk: %s", cmd_chkdsk.c_str());
				_wsystem(cmd_chkdsk.c_str());
			}

			//ft.WriteLine(L"=== set mountpoint");
			//if (!SetVolumeMountPoint(L"C:\\", L"\\\\?\\Volume{45ed89aa-2997-11eb-80b3-806e6f6e6963}\\"))
			//	ft.WriteLine(L"=== set mountpoint failed");
			//system("chkdsk c: /x");
			ft.WriteLine(L"=== set chkdsk OK");
			m_vssClient.registrykey(filelist);
			m_vssClient.SetAsrRestoreStatus(true);
			m_vssClient.PostRestore();
			return 0;
			*/
			
		}

		// backup 
		// Check if the arguments are volumes or file share paths. If yes, try to create the shadow copy set 
		if (IsVolume(arguments[argIndex]) || IsUNCPath((VSS_PWSZ)arguments[argIndex].c_str()))
		{
			opt = 1;
			ft.WriteLine(L"(Option: Create shadow copy set)");
			ft.Trace(DBG_INFO, L"\nAttempting to create a shadow copy set... (volume %s was added as parameter)", arguments[argIndex].c_str());
			//vector<wstring> volumeList;
			volumeList.push_back(GetUniqueVolumeNameForPath(arguments[argIndex], true));
			for (unsigned i = argIndex + 1; i < arguments.size(); i++)
			{
				if (!(IsVolume(arguments[i]) || IsUNCPath((VSS_PWSZ)arguments[i].c_str())))
				{
					volumeList.push_back(GetUniqueVolumeNameForPath(arguments[i], true));
					//if(IsGLOBALROOT((VSS_PWSZ)arguments[i].c_str()))
					//{ 
					//	volumeList.push_back(arguments[i]);
					//	continue;
					//}
					//ft.WriteLine(L"\nERROR: invalid parameters %s", GetCommandLine());
					//return 1;
				}
				//volumeList.push_back(GetUniqueVolumeNameForPath(arguments[i], true));
			}
			break;
			/*
			dwContext = UpdateFinalContext(dwContext);
			m_vssClient.Initialize(dwContext);
			wstring xmlBackupComponentsDoc(METADATA_XML);
			m_vssClient.CreateSnapshotSet(
				volumeList,
				xmlBackupComponentsDoc,
				excludedWriterList,
				includedWriterList
				);

			if ((dwContext & VSS_VOLSNAP_ATTR_DELAYED_POSTSNAPSHOT) == 0)
			{
				// gen raw file
				m_vssClient.vol2raw();

				if ((dwContext & VSS_VOLSNAP_ATTR_NO_WRITERS) == 0)
					m_vssClient.BackupComplete(true);
			}
			ft.WriteLine(L"\nSnapshot creation done.");
			return 0;
			*/
		}
	}

	if (opt == 2)
	{
		Restore(xmlBackupComponentsDoc, dir, filelist);
	}
	else if (opt == 1)
	{
		Backup(volumeList, excludedWriterList, includedWriterList);
	}

	return 0;
}

void CommandLineParser::Backup(vector<wstring> &volList, vector<wstring> &excludedWriterList, vector<wstring> &includedWriterList)
{
	FunctionTracer ft(DBG_INFO);
	if (isEfi())
	{
		//backup efi partiton
		if (m_efi_partition.empty())
		{
			ft.WriteLine(L"please use --efi=xxxx");
			return;
		}
		if (!IsGLOBALROOT((VSS_PWSZ)m_efi_partition.c_str()))
		{
			ft.WriteLine(L"efi partiton format error, must \\?\GLOBALROOT\Device");
			return;
		}

		Copy copy;
		size_t pos = m_efi_partition.find_last_of(L"\\");
		wstring efiname = (pos == wstring::npos) ? m_efi_partition : m_efi_partition.substr(pos + 1);
		if (copy.vol2raw(m_efi_partition, efiname) != 0)
		{
			ft.WriteLine(L"copy efi partiton error");
			return;
		}

	}

	DWORD dwContext = VSS_CTX_BACKUP;
	dwContext = UpdateFinalContext(dwContext);
	m_vssClient.Initialize(dwContext);
	wstring xmlBackupComponentsDoc(METADATA_XML);
	m_vssClient.CreateSnapshotSet(
		volList,
		xmlBackupComponentsDoc,
		excludedWriterList,
		includedWriterList
	);

	if ((dwContext & VSS_VOLSNAP_ATTR_DELAYED_POSTSNAPSHOT) == 0)
	{
		// gen raw file
		m_vssClient.vol2raw();

		if ((dwContext & VSS_VOLSNAP_ATTR_NO_WRITERS) == 0)
			m_vssClient.BackupComplete(true);
	}
	ft.WriteLine(L"\nSnapshot creation done.");
	return;
}

void CommandLineParser::Restore(wstring &meta, wstring &dir, vector<wstring> &filelist)
{
	FunctionTracer ft(DBG_INFO);
	/*
	if (meta.empty() || dir.empty() || filelist.empty())
	{
		ft.WriteLine(L"meta or dir or files not exist");
		return;
	}
	*/

	wstring xmlDoc = ReadFileContents(meta);
	ft.Trace(DBG_INFO, L"XML document: '%s'", xmlDoc.c_str());

	m_vssClient.Initialize(VSS_CTX_ALL, xmlDoc, true);
	m_vssClient.PreRestore();

	// copy vol
	Copy copy;
	wstring srcRaw, dstVol;
	for (vector<wstring>::iterator iter = filelist.begin(); iter != filelist.end(); ++iter)
	{
		srcRaw = dir + *iter;
		if ((*iter)[0] != L'{')
		{
			// efi partition
			wstring path = AppendBackslash(GLOBALROOTPREFIX);
			dstVol = path + *iter;
		}
		else
		{
			dstVol = VOLUMEPREFIX + *iter;
		}
		ft.WriteLine(L"==srcRaw %s ,dstVol %s", srcRaw.c_str(), dstVol.c_str());
		ft.WriteLine(L"\nPress Y to restore %s", dstVol.c_str());
		string flag;
		cin >> flag;
		if (flag == "y" || flag == "Y")
		{
			ft.WriteLine(L"restore vol %s", dstVol.c_str());
			copy.raw2vol(srcRaw, dstVol);
		}
	}

	// chkdsk
	//wstring cmd_chkdsk;
	//for (vector<wstring>::iterator iter = filelist.begin(); iter != filelist.end(); ++iter)
	//{
	//	if ((*iter)[0] != L'{')
	//		continue;

	//	dstVol = VOLUMEPREFIX + *iter;
	//	cmd_chkdsk = L"chkdsk " + dstVol + L" /X";
	//	ft.WriteLine(L"cmd_chkdsk: %s", cmd_chkdsk.c_str());
	//	_wsystem(cmd_chkdsk.c_str());
	//}
	ft.WriteLine(L"=== set chkdsk OK3");

	m_vssClient.Registrykey(filelist);
	m_vssClient.SetAsrRestoreStatus(true);
	m_vssClient.PostRestore();
	return;

}

bool CommandLineParser::isEfi()
{
	FunctionTracer ft(DBG_INFO);
	FIRMWARE_TYPE a;
	bool isefi = false;
	GetFirmwareType(&a);
	switch (a)
	{
	case FirmwareTypeUefi:
		ft.WriteLine(L"===is efi");
		isefi = true;
		break;
	case FirmwareTypeBios:
		ft.WriteLine(L"===is bios");
		break;
	default:
		break;
	}
	return isefi;
}

int wmain(int argc, WCHAR ** argv)
{
	FunctionTracer ft(DBG_INFO);
	vector<wstring> arguments;
	CommandLineParser obj;
	for (int i = 1; i < argc; i++)
		arguments.push_back(argv[i]);

	try
	{
		return obj.MainRoutine(arguments);
	}
	catch (bad_alloc ex)
	{
		// Generic STL allocation error
		ft.WriteLine(L"ERROR: Memory allocation error");
		return 3;
	}
	catch (exception ex)
	{
		// We should never get here (unless we have a bug)
		_ASSERTE(false);
		ft.WriteLine(L"ERROR: STL Exception caught: %S", ex.what());
		return 2;
	}
	catch (HRESULT hr)
	{
		ft.Trace(DBG_INFO, L"HRESULT Error caught: 0x%08lx", hr);
		return 2;
	}
}

