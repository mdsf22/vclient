#pragma once
#include "stdafx.h"

class CommandLineParser
{
public:
	CommandLineParser();
	~CommandLineParser();

	// Main routine 
	int MainRoutine(vector<wstring> arguments);
private:
	bool isEfi();

	DWORD UpdateFinalContext(DWORD dwContext);
	void Backup(vector<wstring> &volList, vector<wstring> &excludedWriterList, vector<wstring> &includedWriterList);
	void Restore(wstring &meta, wstring &dir, vector<wstring> &files);
	bool MatchArgument(wstring arg, wstring optionPattern);
	bool MatchArgument(wstring argument, wstring optionPattern, wstring & additionalParameter);
	VssClient   m_vssClient;

	bool        m_bPersistent;
	bool        m_bWithWriters;
	bool        m_bWaitForFinish;
	wstring     m_efi_partition;

};