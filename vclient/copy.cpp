#include "copy.h"

# define _1M                                 0x00100000
# define VERR_EOF                            (-110)

Copy::Copy()
{

}

Copy::~Copy()
{

}

int Copy::raw2vol(wstring src, wstring dst)
{
	FunctionTracer ft(DBG_INFO);
	HANDLE hVol;
	//hVol = CreateFile(L"\\\\.\\I:", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	hVol = CreateFile(dst.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hVol == INVALID_HANDLE_VALUE)
	{
		ft.WriteLine(L"Create dst File %s failed, %d", dst.c_str(), GetLastError());
		return -1;
	}
	ULONG                   bytesRet;
	//DeviceIoControl(hVol, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &bytesRet, NULL);
	ft.WriteLine(L"no DeviceIoControl");
	if (!DeviceIoControl(hVol, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL))
	{
		ft.WriteLine(L"Could not dismount volume, %d", GetLastError());
	}
	ft.WriteLine(L"dismount volume");
	HANDLE hRaw;
	hRaw = CreateFile(src.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hRaw == INVALID_HANDLE_VALUE)
	{
		ft.WriteLine(L"Create src File %s failed", src.c_str());
		return -1;
	}

	uint64_t filesize;
	int rc = getFileSize(hRaw, &filesize);
	if (rc != 0)
	{
		ft.WriteLine(L"get FileSize %s failed", src.c_str());
		return -1;
	}

	ft.WriteLine(L"FileSize %s %llu ", src.c_str(), filesize);
	size_t cbBuffer = _1M;
	void *pvBuf = NULL;
	pvBuf = malloc(cbBuffer);
	uint64_t offFile;
	offFile = 0;
	DWORD dwBytesWritten = 0;
	uint64_t total_read = 0;
	while (offFile < filesize)
	{
		size_t cbRead;
		size_t cbToRead;
		cbRead = 0;
		cbToRead = filesize - offFile >= (uint64_t)cbBuffer ?
			cbBuffer : (size_t)(filesize - offFile);
		rc = fileRead(hRaw, pvBuf, cbToRead, &cbRead);
		if (rc != 0 || !cbRead)
			break;
		total_read += cbRead;
		WriteFile(hVol, pvBuf, cbRead, &dwBytesWritten, NULL);
		offFile += cbRead;
	}
	ft.WriteLine(L"write %llu ", total_read);
	if (pvBuf)
	{
		free(pvBuf);
	}
	CloseHandle(hRaw);
	CloseHandle(hVol);
	return 0;
}

int Copy::vol2raw(wstring src, wstring dst)
{
	FunctionTracer ft(DBG_INFO);
	HANDLE hRaw;
	//hRaw = CreateFile(dst.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	hRaw = CreateFile(dst.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hRaw == INVALID_HANDLE_VALUE)
	{
		ft.WriteLine(L"Create dst File %s failed", dst.c_str());
		return -1;
	}

	HANDLE hVol;
	//hVol = CreateFile(src.c_str(), GENERIC_READ , 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	hVol = CreateFile(src.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hVol == INVALID_HANDLE_VALUE)
	{
		ft.WriteLine(L"Create src File %s failed", src.c_str());
		return -1;
	}
	uint64_t filesize;
	int rc = getFileSize(hVol, &filesize);
	if (rc != 0)
	{
		ft.WriteLine(L"get FileSize %s failed", src.c_str());
		return -1;
	}
	ULONG                   bytesRet;
	//DeviceIoControl(hVol, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &bytesRet, NULL);
	ft.WriteLine(L"no DeviceIoControl");
	ft.WriteLine(L"FileSize %s %llu ", src.c_str(), filesize);

	size_t cbBuffer = _1M;
	void *pvBuf = NULL;
	pvBuf = malloc(cbBuffer);

	uint64_t offFile;
	offFile = 0;
	DWORD dwBytesWritten = 0;
	uint64_t total_read = 0;
	while (offFile < filesize)
	{
		size_t cbRead;
		size_t cbToRead;
		cbRead = 0;
		cbToRead = filesize - offFile >= (uint64_t)cbBuffer ?
			cbBuffer : (size_t)(filesize - offFile);
		rc = fileRead(hVol, pvBuf, cbToRead, &cbRead);
		if (rc != 0 || !cbRead)
			break;
		total_read += cbRead;
		WriteFile(hRaw, pvBuf, cbRead, &dwBytesWritten, NULL);
		offFile += cbRead;
	}
	ft.WriteLine(L"==== write %llu ", total_read);
	free(pvBuf);
	CloseHandle(hVol);
	CloseHandle(hRaw);
	return 0;
}

int Copy::getFileSize(HANDLE &hFile, uint64_t *pcbSize)
{
	ULARGE_INTEGER  Size;
	Size.LowPart = GetFileSize(hFile, &Size.HighPart);
	if (Size.LowPart != INVALID_FILE_SIZE)
	{
		*pcbSize = Size.QuadPart;
		return 0;
	}
	int rc = -1;

	DISK_GEOMETRY   DriveGeo;
	DWORD           cbDriveGeo;
	if (DeviceIoControl(hFile,
		IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
		&DriveGeo, sizeof(DriveGeo), &cbDriveGeo, NULL))
	{
		if (DriveGeo.MediaType == FixedMedia
			|| DriveGeo.MediaType == RemovableMedia)
		{
			*pcbSize = DriveGeo.Cylinders.QuadPart
				* DriveGeo.TracksPerCylinder
				* DriveGeo.SectorsPerTrack
				* DriveGeo.BytesPerSector;

			GET_LENGTH_INFORMATION  DiskLenInfo;
			DWORD                   Ignored;
			if (DeviceIoControl(hFile,
				IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
				&DiskLenInfo, sizeof(DiskLenInfo), &Ignored, (LPOVERLAPPED)NULL))
			{
				*pcbSize = DiskLenInfo.Length.QuadPart;
			}
			return 0;
		}
	}
	return rc;
}

int Copy::fileRead(HANDLE &hFile, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
	if (cbToRead <= 0)
		return 0;
	ULONG cbToReadAdj = (ULONG)cbToRead;

	ULONG cbRead = 0;
	if (ReadFile(hFile, pvBuf, cbToReadAdj, &cbRead, NULL))
	{
		if (pcbRead)
			*pcbRead = cbRead;
		else
		{
			while (cbToReadAdj > cbRead)
			{
				ULONG cbReadPart = 0;
				if (!ReadFile(hFile, (char*)pvBuf + cbRead, cbToReadAdj - cbRead, &cbReadPart, NULL))
					return -1;
				if (cbReadPart == 0)
					return VERR_EOF;
				cbRead += cbReadPart;
			}
		}
		return 0;
	}
	return -1;
}
