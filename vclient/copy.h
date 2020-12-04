#pragma once
#include "stdafx.h"

class Copy 
{
public:
	Copy();

	~Copy();

	int vol2raw(wstring src, wstring dst);

	int raw2vol(wstring src, wstring dst);

private:
	int getFileSize(HANDLE &hFile, uint64_t *pcbSize);

	int fileRead(HANDLE &hFile, void *pvBuf, size_t cbToRead, size_t *pcbRead);
};