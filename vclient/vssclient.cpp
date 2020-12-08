#include "stdafx.h"
#include <winreg.h>
#include <ctime>
#define ASR_WRITER_XML L"asr.xml"

VssClient::VssClient()
{
	m_bCoInitializeCalled = false;
	m_dwContext = VSS_CTX_BACKUP;
	m_latestSnapshotSetID = GUID_NULL;
	m_bDuringRestore = false;
}


VssClient::~VssClient()
{
	// Release the IVssBackupComponents interface 
	// WARNING: this must be done BEFORE calling CoUninitialize()
	m_pVssObject = NULL;

	// Call CoUninitialize if the CoInitialize was performed sucesfully
	if (m_bCoInitializeCalled)
		CoUninitialize();
}

void VssClient::Initialize(DWORD dwContext, wstring xmlDoc, bool bDuringRestore)
{
	FunctionTracer ft(DBG_INFO);

	// Initialize COM 
	CHECK_COM(CoInitialize(NULL));
	m_bCoInitializeCalled = true;

	// Initialize COM security
	CHECK_COM(
		CoInitializeSecurity(
			NULL,                           //  Allow *all* VSS writers to communicate back!
			-1,                             //  Default COM authentication service
			NULL,                           //  Default COM authorization service
			NULL,                           //  reserved parameter
			RPC_C_AUTHN_LEVEL_PKT_PRIVACY,  //  Strongest COM authentication level
			RPC_C_IMP_LEVEL_IMPERSONATE,    //  Minimal impersonation abilities 
			NULL,                           //  Default COM authentication settings
			EOAC_DYNAMIC_CLOAKING,          //  Cloaking
			NULL                            //  Reserved parameter
			));

	// Create the internal backup components object
	CHECK_COM(CreateVssBackupComponents(&m_pVssObject));

	// We are during restore now?
	m_bDuringRestore = bDuringRestore;

	// Call either Initialize for backup or for restore
	if (m_bDuringRestore)
	{
		CHECK_COM(m_pVssObject->InitializeForRestore(CComBSTR(xmlDoc.c_str())))
	}
	else
	{
		// Initialize for backup
		if (xmlDoc.length() == 0)
			CHECK_COM(m_pVssObject->InitializeForBackup())
		else
			CHECK_COM(m_pVssObject->InitializeForBackup(CComBSTR(xmlDoc.c_str())))

		// Set the context, if different than the default context
		if (dwContext != VSS_CTX_BACKUP)
		{
			ft.WriteLine(L"- Setting the VSS context to: 0x%08lx", dwContext);
			CHECK_COM(m_pVssObject->SetContext(dwContext));
		}
		CHECK_COM(m_pVssObject->SetBackupState(true, true, VSS_BT_FULL, false));
	}
	// Keep the context
	m_dwContext = dwContext;

}

void VssClient::WaitAndCheckForAsyncOperation(IVssAsync* pAsync)
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"(Waiting for the asynchronous operation to finish...)");

	// Wait until the async operation finishes
	CHECK_COM(pAsync->Wait());

	// Check the result of the asynchronous operation
	HRESULT hrReturned = S_OK;
	CHECK_COM(pAsync->QueryStatus(&hrReturned, NULL));

	// Check if the async operation succeeded...
	if (FAILED(hrReturned))
	{
		ft.WriteLine(L"Error during the last asynchronous operation.");
		ft.WriteLine(L"- Returned HRESULT = 0x%08lx", hrReturned);
		ft.WriteLine(L"- Error text: %s", FunctionTracer::HResult2String(hrReturned).c_str());
		ft.WriteLine(L"- Please re-run VSHADOW.EXE with the /tracing option to get more details");
		throw(hrReturned);
	}
}

void VssClient::CreateSnapshotSet(
	vector<wstring> volumeList,
	wstring outputXmlFile,
	vector<wstring> excludedWriterList,
	vector<wstring> includedWriterList
	)
{
	FunctionTracer ft(DBG_INFO);

	bool bSnapshotWithWriters = ((m_dwContext & VSS_VOLSNAP_ATTR_NO_WRITERS) == 0);

	if (bSnapshotWithWriters)
		GatherWriterMetadata();

	if (bSnapshotWithWriters)
		SelectComponentsForBackup(volumeList, excludedWriterList, includedWriterList);

	ft.WriteLine(L" ===AddComponent OK");
	// Start the shadow set
	CHECK_COM(m_pVssObject->StartSnapshotSet(&m_latestSnapshotSetID))
		ft.WriteLine(L"Creating shadow set " WSTR_GUID_FMT L" ...", GUID_PRINTF_ARG(m_latestSnapshotSetID));
	ft.WriteLine(L" ===StartSnapshotSet OK");

	// save asr xml
	//SaveAsrXml();
	//ft.WriteLine(L" ===saveAsrXml OK");
	// Add the specified volumes to the shadow set
	AddToSnapshotSet(volumeList);
	ft.WriteLine(L" ===AddToSnapshotSet OK");
	// Prepare for backup. 
	// This will internally create the backup components document with the selected components
	if (bSnapshotWithWriters)
		PrepareForBackup();
	ft.WriteLine(L" ===PrepareForBackup OK");
	// Creates the shadow set 
	DoSnapshotSet();
	ft.WriteLine(L" ===DoSnapshotSet OK");

	// Do not attempt to continue with delayed snapshot ...
	if (m_dwContext & VSS_VOLSNAP_ATTR_DELAYED_POSTSNAPSHOT)
	{
		ft.WriteLine(L"\nFast snapshot created. Exiting... \n");
		return;
	}

	// Saves the backup components document, if needed
	if (outputXmlFile.length() > 0)
		SaveBackupComponentsDocument(outputXmlFile);

	// List all the created shadow copies
	if ((m_dwContext & VSS_VOLSNAP_ATTR_TRANSPORTABLE) == 0)
	{
		ft.WriteLine(L"\nList of created shadow copies: \n");
		QuerySnapshotSet(m_latestSnapshotSetID);
	}

}

void VssClient::BackupComplete(bool succeeded)
{
	FunctionTracer ft(DBG_INFO);

	unsigned cWriters = 0;
	CHECK_COM(m_pVssObject->GetWriterComponentsCount(&cWriters));

	if (cWriters == 0)
	{
		ft.WriteLine(L"- There were no writer components in this backup");
		return;
	}
	else if (succeeded)
		ft.WriteLine(L"- Mark all writers as succesfully backed up... ");
	else
		ft.WriteLine(L"- Backup failed. Mark all writers as not succesfully backed up... ");

	SetBackupSucceeded(succeeded);

	ft.WriteLine(L"Completing the backup (BackupComplete) ... ");

	CComPtr<IVssAsync>  pAsync;
	CHECK_COM(m_pVssObject->BackupComplete(&pAsync));

	// Waits for the async operation to finish and checks the result
	WaitAndCheckForAsyncOperation(pAsync);

	// Check selected writer status
	CheckSelectedWriterStatus();

}

void VssClient::SetBackupSucceeded(bool succeeded)
{
	FunctionTracer ft(DBG_INFO);

	// Enumerate writers
	for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
	{
		VssWriter & writer = m_writerList[iWriter];

		// Enumerate components
		for (unsigned iComponent = 0; iComponent < writer.components_.size(); iComponent++)
		{
			VssComponent & component = writer.components_[iComponent];

			// Test that the component is explicitely selected and requires notification
			if (!component.isExplicitlyIncluded_)
				continue;

			// Call SetBackupSucceeded for this component
			CHECK_COM(m_pVssObject->SetBackupSucceeded(
				WString2Guid(writer.instanceId_),
				WString2Guid(writer.id_),
				component.type_,
				component.logicalPath_.c_str(),
				component.name_.c_str(),
				succeeded));
		}
	}
}

// Prepare the shadow for backup
void VssClient::PrepareForBackup()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"Preparing for backup ... ");

	CComPtr<IVssAsync>  pAsync;
	CHECK_COM(m_pVssObject->PrepareForBackup(&pAsync));

	// Waits for the async operation to finish and checks the result
	WaitAndCheckForAsyncOperation(pAsync);

	// Check selected writer status
	CheckSelectedWriterStatus();
}

// Save the backup components document
void VssClient::SaveBackupComponentsDocument(wstring fileName)
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"Saving the backup components document ... ");

	// Get the Backup Components in XML format
	CComBSTR bstrXML;
	CHECK_COM(m_pVssObject->SaveAsXML(&bstrXML));

	// Save the XML string to the file
	WriteFile(fileName, BSTR2WString(bstrXML));
}

// Effectively creating the shadow (calling DoSnapshotSet)
void VssClient::DoSnapshotSet()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"Creating the shadow (DoSnapshotSet) ... ");

	CComPtr<IVssAsync>  pAsync;
	CHECK_COM(m_pVssObject->DoSnapshotSet(&pAsync));

	// Waits for the async operation to finish and checks the result
	WaitAndCheckForAsyncOperation(pAsync);

	// Do not attempt to continue with delayed snapshot ...
	if (m_dwContext & VSS_VOLSNAP_ATTR_DELAYED_POSTSNAPSHOT)
	{
		ft.WriteLine(L"\nFast DoSnapshotSet finished. \n");
		return;
	}

	// Check selected writer status
	CheckSelectedWriterStatus();

	ft.WriteLine(L"Shadow copy set succesfully created.");
}

void VssClient::AddToSnapshotSet(vector<wstring> volumeList)
{
	FunctionTracer ft(DBG_INFO);

	// Preserve the list of volumes for script generation 
	m_latestVolumeList = volumeList;

	_ASSERTE(m_latestSnapshotIdList.size() == 0);

	// Add volumes to the shadow set 
	for (unsigned i = 0; i < volumeList.size(); i++)
	{
		wstring volume = volumeList[i];
		ft.WriteLine(L"- Adding volume %s [%s] to the shadow set...",
			volume.c_str(),
			GetDisplayNameForVolume(volume).c_str());

		VSS_ID SnapshotID;
		CHECK_COM(m_pVssObject->AddToSnapshotSet((LPWSTR)volume.c_str(), GUID_NULL, &SnapshotID));

		// Preserve this shadow ID for script generation 
		m_latestSnapshotIdList.push_back(SnapshotID);
	}
}

// Query all the shadow copies in the given set
// If snapshotSetID is NULL, just query all shadow copies in the system
void VssClient::QuerySnapshotSet(VSS_ID snapshotSetID)
{
	FunctionTracer ft(DBG_INFO);

	if (snapshotSetID == GUID_NULL)
		ft.WriteLine(L"\nQuerying all shadow copies in the system ...\n");
	else
		ft.WriteLine(L"\nQuerying all shadow copies with the SnapshotSetID " WSTR_GUID_FMT L" ...\n", GUID_PRINTF_ARG(snapshotSetID));

	// Get list all shadow copies. 
	CComPtr<IVssEnumObject> pIEnumSnapshots;
	HRESULT hr = m_pVssObject->Query(GUID_NULL,
		VSS_OBJECT_NONE,
		VSS_OBJECT_SNAPSHOT,
		&pIEnumSnapshots);

	CHECK_COM_ERROR(hr, L"m_pVssObject->Query(GUID_NULL, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &pIEnumSnapshots )")

		// If there are no shadow copies, just return
		if (hr == S_FALSE) {
			if (snapshotSetID == GUID_NULL)
				ft.WriteLine(L"\nThere are no shadow copies in the system\n");
			return;
		}

	// Enumerate all shadow copies. 
	VSS_OBJECT_PROP Prop;
	VSS_SNAPSHOT_PROP& Snap = Prop.Obj.Snap;

	while (true)
	{
		// Get the next element
		ULONG ulFetched;
		hr = pIEnumSnapshots->Next(1, &Prop, &ulFetched);
		CHECK_COM_ERROR(hr, L"pIEnumSnapshots->Next( 1, &Prop, &ulFetched )")

			// We reached the end of list
			if (ulFetched == 0)
				break;

		// Automatically call VssFreeSnapshotProperties on this structure at the end of scope
		CAutoSnapPointer snapAutoCleanup(&Snap);

		// Print the shadow copy (if not filtered out)
		if ((snapshotSetID == GUID_NULL) || (Snap.m_SnapshotSetId == snapshotSetID))
			PrintSnapshotProperties(Snap);
	}
}

// Gather writers status
void VssClient::GatherWriterStatus()
{
	FunctionTracer ft(DBG_INFO);

	// Gathers writer status
	// WARNING: GatherWriterMetadata must be called before
	CComPtr<IVssAsync>  pAsync;
	CHECK_COM(m_pVssObject->GatherWriterStatus(&pAsync));

	// Waits for the async operation to finish and checks the result
	WaitAndCheckForAsyncOperation(pAsync);
}

// Returns TRUE if the writer was previously selected
bool VssClient::IsWriterSelected(GUID guidInstanceId)
{
	// If this writer was not selected for backup, ignore it
	wstring instanceId = Guid2WString(guidInstanceId);
	for (unsigned i = 0; i < m_writerList.size(); i++)
		if ((instanceId == m_writerList[i].instanceId_) && !m_writerList[i].isExcluded_)
			return true;

	return false;
}

// Check the status for all selected writers
void VssClient::CheckSelectedWriterStatus()
{
	FunctionTracer ft(DBG_INFO);

	if ((m_dwContext & VSS_VOLSNAP_ATTR_NO_WRITERS) != 0)
		return;

	// Gather writer status to detect potential errors
	GatherWriterStatus();

	// Gets the number of writers in the gathered status info
	// (WARNING: GatherWriterStatus must be called before)
	unsigned cWriters = 0;
	CHECK_COM(m_pVssObject->GetWriterStatusCount(&cWriters));

	// Enumerate each writer
	for (unsigned iWriter = 0; iWriter < cWriters; iWriter++)
	{
		VSS_ID idInstance = GUID_NULL;
		VSS_ID idWriter = GUID_NULL;
		VSS_WRITER_STATE eWriterStatus = VSS_WS_UNKNOWN;
		CComBSTR bstrWriterName;
		HRESULT hrWriterFailure = S_OK;

		// Get writer status
		CHECK_COM(m_pVssObject->GetWriterStatus(iWriter,
			&idInstance,
			&idWriter,
			&bstrWriterName,
			&eWriterStatus,
			&hrWriterFailure));

		// If the writer is not selected, just continue
		if (!IsWriterSelected(idInstance))
			continue;

		// If the writer is in non-stable state, break
		switch (eWriterStatus)
		{
		case VSS_WS_FAILED_AT_IDENTIFY:
		case VSS_WS_FAILED_AT_PREPARE_BACKUP:
		case VSS_WS_FAILED_AT_PREPARE_SNAPSHOT:
		case VSS_WS_FAILED_AT_FREEZE:
		case VSS_WS_FAILED_AT_THAW:
		case VSS_WS_FAILED_AT_POST_SNAPSHOT:
		case VSS_WS_FAILED_AT_BACKUP_COMPLETE:
		case VSS_WS_FAILED_AT_PRE_RESTORE:
		case VSS_WS_FAILED_AT_POST_RESTORE:
		case VSS_WS_FAILED_AT_BACKUPSHUTDOWN:
			break;

		default:
			continue;
		}

		// Print writer status
		ft.WriteLine(L"\n"
			L"ERROR: Selected writer '%s' is in failed state!\n"
			L"   - Status: %d (%s)\n"
			L"   - Writer Failure code: 0x%08lx (%s)\n"
			L"   - Writer ID: " WSTR_GUID_FMT L"\n"
			L"   - Instance ID: " WSTR_GUID_FMT L"\n",
			(PWCHAR)bstrWriterName,
			eWriterStatus, GetStringFromWriterStatus(eWriterStatus).c_str(),
			hrWriterFailure, FunctionTracer::HResult2String(hrWriterFailure).c_str(),
			GUID_PRINTF_ARG(idWriter),
			GUID_PRINTF_ARG(idInstance)
			);

		// Stop here
		throw(E_UNEXPECTED);
	}
}

// Convert a writer status into a string
wstring VssClient::GetStringFromWriterStatus(VSS_WRITER_STATE eWriterStatus)
{
	FunctionTracer ft(DBG_INFO);

	ft.Trace(DBG_INFO, L"Interpreting constant %d", (int)eWriterStatus);
	switch (eWriterStatus)
	{
		CHECK_CASE_FOR_CONSTANT(VSS_WS_STABLE);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_FREEZE);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_THAW);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_POST_SNAPSHOT);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_BACKUP_COMPLETE);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_IDENTIFY);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_PREPARE_BACKUP);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_PREPARE_SNAPSHOT);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_FREEZE);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_THAW);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_POST_SNAPSHOT);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_BACKUP_COMPLETE);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_PRE_RESTORE);
		CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_POST_RESTORE);

	default:
		ft.WriteLine(L"Unknown constant: %d", eWriterStatus);
		_ASSERTE(false);
		return wstring(L"Undefined");
	}
}

// Print the properties for the given snasphot
void VssClient::PrintSnapshotProperties(VSS_SNAPSHOT_PROP & prop)
{
	FunctionTracer ft(DBG_INFO);

	LONG lAttributes = prop.m_lSnapshotAttributes;

	ft.WriteLine(L"* SNAPSHOT ID = " WSTR_GUID_FMT L" ...", GUID_PRINTF_ARG(prop.m_SnapshotId));
	ft.WriteLine(L"   - Shadow copy Set: " WSTR_GUID_FMT, GUID_PRINTF_ARG(prop.m_SnapshotSetId));
	ft.WriteLine(L"   - Original count of shadow copies = %d", prop.m_lSnapshotsCount);
	ft.WriteLine(L"   - Original Volume name: %s [%s]",
		prop.m_pwszOriginalVolumeName,
		GetDisplayNameForVolume(prop.m_pwszOriginalVolumeName).c_str()
		);
	ft.WriteLine(L"   - Creation Time: %s", VssTimeToString(prop.m_tsCreationTimestamp).c_str());
	ft.WriteLine(L"   - Shadow copy device name: %s", prop.m_pwszSnapshotDeviceObject);
	ft.WriteLine(L"   - Originating machine: %s", prop.m_pwszOriginatingMachine);
	ft.WriteLine(L"   - Service machine: %s", prop.m_pwszServiceMachine);

	if (prop.m_lSnapshotAttributes & VSS_VOLSNAP_ATTR_EXPOSED_LOCALLY)
		ft.WriteLine(L"   - Exposed locally as: %s", prop.m_pwszExposedName);
	else if (prop.m_lSnapshotAttributes & VSS_VOLSNAP_ATTR_EXPOSED_REMOTELY)
	{
		ft.WriteLine(L"   - Exposed remotely as %s", prop.m_pwszExposedName);
		if (prop.m_pwszExposedPath && wcslen(prop.m_pwszExposedPath) > 0)
			ft.WriteLine(L"   - Path exposed: %s", prop.m_pwszExposedPath);
	}
	else
		ft.WriteLine(L"   - Not Exposed");

	ft.WriteLine(L"   - Provider id: " WSTR_GUID_FMT, GUID_PRINTF_ARG(prop.m_ProviderId));

	// Display the attributes
	wstring attributes;
	if (lAttributes & VSS_VOLSNAP_ATTR_TRANSPORTABLE)
		attributes += wstring(L" Transportable");

	if (lAttributes & VSS_VOLSNAP_ATTR_NO_AUTO_RELEASE)
		attributes += wstring(L" No_Auto_Release");
	else
		attributes += wstring(L" Auto_Release");

	if (lAttributes & VSS_VOLSNAP_ATTR_PERSISTENT)
		attributes += wstring(L" Persistent");

	if (lAttributes & VSS_VOLSNAP_ATTR_CLIENT_ACCESSIBLE)
		attributes += wstring(L" Client_accessible");

	if (lAttributes & VSS_VOLSNAP_ATTR_HARDWARE_ASSISTED)
		attributes += wstring(L" Hardware");

	if (lAttributes & VSS_VOLSNAP_ATTR_NO_WRITERS)
		attributes += wstring(L" No_Writers");

	if (lAttributes & VSS_VOLSNAP_ATTR_IMPORTED)
		attributes += wstring(L" Imported");

	if (lAttributes & VSS_VOLSNAP_ATTR_PLEX)
		attributes += wstring(L" Plex");

	if (lAttributes & VSS_VOLSNAP_ATTR_DIFFERENTIAL)
		attributes += wstring(L" Differential");

	ft.WriteLine(L"   - Attributes: %s", attributes.c_str());

	ft.WriteLine(L"");
}

void VssClient::GatherWriterMetadata()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"(Gathering writer metadata...)");

	// Gathers writer metadata
	// WARNING: this call can be performed only once per IVssBackupComponents instance!
	CComPtr<IVssAsync>  pAsync;
	CHECK_COM(m_pVssObject->GatherWriterMetadata(&pAsync));

	// Waits for the async operation to finish and checks the result
	WaitAndCheckForAsyncOperation(pAsync);

	ft.WriteLine(L"Initialize writer metadata ...");

	// Initialize the internal metadata data structures
	InitializeWriterMetadata();
}

void VssClient::InitializeWriterMetadata()
{
	FunctionTracer ft(DBG_INFO);

	// Get the list of writers in the metadata  
	unsigned cWriters = 0;
	CHECK_COM(m_pVssObject->GetWriterMetadataCount(&cWriters));

	// Enumerate writers
	for (unsigned iWriter = 0; iWriter < cWriters; iWriter++)
	{
		//if (iWriter == 3)
		//	continue;
		//Get the metadata for this particular writer
		VSS_ID idInstance = GUID_NULL;
		CComPtr<IVssExamineWriterMetadata> pMetadata;
		CHECK_COM(m_pVssObject->GetWriterMetadata(iWriter, &idInstance, &pMetadata));

		VssWriter   writer;
		writer.Initialize(pMetadata);
		// Add this writer to the list 
		//if (writer.name_ == L"ASR Writer")
		//{
		//	//CComBSTR bstrXML;
		//	//CHECK_COM(pMetadata->SaveAsXML(&bstrXML));
		//	//WriteFile(ASR_WRITER_XML, BSTR2WString(bstrXML));
		//	m_writerList.push_back(writer);
		//}
		m_writerList.push_back(writer);
	}
}

void VssClient::DiscoverDirectlyExcludedComponents(
	vector<wstring> excludedWriterAndComponentList,
	vector<VssWriter> & writerList
	)
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"Discover directly excluded components ...");

	// Discover components that should be excluded from the shadow set 
	// This means components that have at least one File Descriptor requiring 
	// volumes not in the shadow set. 
	for (unsigned iWriter = 0; iWriter < writerList.size(); iWriter++)
	{
		VssWriter & writer = writerList[iWriter];

		// Check if the writer is excluded
		if (FindStringInList(writer.name_, excludedWriterAndComponentList) ||
			FindStringInList(writer.id_, excludedWriterAndComponentList) ||
			FindStringInList(writer.instanceId_, excludedWriterAndComponentList))
		{
			writer.isExcluded_ = true;
			continue;
		}

		// Check if the component is excluded
		for (unsigned iComponent = 0; iComponent < writer.components_.size(); iComponent++)
		{
			VssComponent & component = writer.components_[iComponent];

			// Check to see if this component is explicitly excluded

			// Compute various component paths
			// Format: Writer:logicaPath\componentName
			wstring componentPathWithWriterName = writer.name_ + L":" + component.fullPath_;
			wstring componentPathWithWriterID = writer.id_ + L":" + component.fullPath_;
			wstring componentPathWithWriterIID = writer.instanceId_ + L":" + component.fullPath_;

			// Check to see if this component is explicitly excluded
			if (FindStringInList(componentPathWithWriterName, excludedWriterAndComponentList) ||
				FindStringInList(componentPathWithWriterID, excludedWriterAndComponentList) ||
				FindStringInList(componentPathWithWriterIID, excludedWriterAndComponentList))
			{
				ft.WriteLine(L"- Component '%s' from writer '%s' is explicitly excluded from backup ",
					component.fullPath_.c_str(), writer.name_.c_str());

				component.isExcluded_ = true;
				continue;
			}
		}

		// Now, discover if we have any selected components. If none, exclude the whole writer
		bool nonExcludedComponents = false;
		for (unsigned i = 0; i < writer.components_.size(); i++)
		{
			VssComponent & component = writer.components_[i];

			if (!component.isExcluded_)
				nonExcludedComponents = true;
		}

		// If all components are missing or excluded, then exclude the writer too
		if (!nonExcludedComponents)
		{
			ft.WriteLine(L"- Excluding writer '%s' since it has no selected components for restore.", writer.name_.c_str());
			writer.isExcluded_ = true;
		}
	}
}

void VssClient::DiscoverNonShadowedExcludedComponents(
	vector<wstring> shadowSourceVolumes
	)
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"Discover components that reside outside the shadow set ...");

	// Discover components that should be excluded from the shadow set 
	// This means components that have at least one File Descriptor requiring 
	// volumes not in the shadow set. 
	for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
	{
		VssWriter & writer = m_writerList[iWriter];

		// Check if the writer is excluded
		if (writer.isExcluded_)
			continue;

		// Check if the component is excluded
		for (unsigned iComponent = 0; iComponent < writer.components_.size(); iComponent++)
		{
			VssComponent & component = writer.components_[iComponent];

			// Check to see if this component is explicitly excluded
			if (component.isExcluded_)
				continue;

			// Try to find an affected volume outside the shadow set
			// If yes, exclude the component
			for (unsigned iVol = 0; iVol < component.affectedVolumes_.size(); iVol++)
			{
				if (ClusterIsPathOnSharedVolume(component.affectedVolumes_[iVol].c_str()))
				{
					wstring wsUniquePath(MAX_PATH, L'\0');
					ClusterGetVolumeNameForVolumeMountPoint(component.affectedVolumes_[iVol].c_str(),
						WString2Buffer(wsUniquePath),
						(DWORD)wsUniquePath.length());

					component.affectedVolumes_[iVol] = wsUniquePath;
				}

				if (!FindStringInList(component.affectedVolumes_[iVol], shadowSourceVolumes))
				{
					wstring wsLocalVolume;

					if (GetDisplayNameForVolumeNoThrow(component.affectedVolumes_[iVol], wsLocalVolume))
						ft.WriteLine(L"- Component '%s' from writer '%s' is excluded from backup (it requires %s in the shadow set)",
							component.fullPath_.c_str(), writer.name_.c_str(), wsLocalVolume.c_str());
					else
						ft.WriteLine(L"- Component '%s' from writer '%s' is excluded from backup", component.fullPath_.c_str(), writer.name_.c_str());

					component.isExcluded_ = true;
					break;
				}
			}
		}
	}
}

void VssClient::DiscoverAllExcludedComponents()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"Discover all excluded components ...");

	// Discover components that should be excluded from the shadow set 
	// This means components that have at least one File Descriptor requiring 
	// volumes not in the shadow set. 
	for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
	{
		VssWriter & writer = m_writerList[iWriter];
		if (writer.isExcluded_)
			continue;

		// Enumerate all components
		for (unsigned i = 0; i < writer.components_.size(); i++)
		{
			VssComponent & component = writer.components_[i];

			// Check if this component has any excluded children
			// If yes, deselect it
			for (unsigned j = 0; j < writer.components_.size(); j++)
			{
				VssComponent & descendent = writer.components_[j];
				if (component.IsAncestorOf(descendent) && descendent.isExcluded_)
				{
					ft.WriteLine(L"- Component '%s' from writer '%s' is excluded from backup "
						L"(it has an excluded descendent: '%s')",
						component.fullPath_.c_str(), writer.name_.c_str(), descendent.name_.c_str());

					component.isExcluded_ = true;
					break;
				}
			}
		}
	}
}

void VssClient::DiscoverExcludedWriters()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"Discover excluded writers ...");

	// Enumerate writers
	for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
	{
		VssWriter & writer = m_writerList[iWriter];
		if (writer.isExcluded_)
			continue;

		// Discover if we have any:
		// - non-excluded selectable components 
		// - or non-excluded top-level non-selectable components
		// If we have none, then the whole writer must be excluded from the backup
		writer.isExcluded_ = true;
		for (unsigned i = 0; i < writer.components_.size(); i++)
		{
			VssComponent & component = writer.components_[i];
			if (component.CanBeExplicitlyIncluded())
			{
				writer.isExcluded_ = false;
				break;
			}
		}

		// No included components were found
		if (writer.isExcluded_)
		{
			ft.WriteLine(L"- The writer '%s' is now entirely excluded from the backup:", writer.name_.c_str());
			ft.WriteLine(L"  (it does not contain any components that can be potentially included in the backup)");
			continue;
		}

		// Now, discover if we have any top-level excluded non-selectable component 
		// If this is true, then the whole writer must be excluded from the backup
		for (unsigned i = 0; i < writer.components_.size(); i++)
		{
			VssComponent & component = writer.components_[i];

			if (component.isTopLevel_ && !component.isSelectable_ && component.isExcluded_)
			{
				ft.WriteLine(L"- The writer '%s' is now entirely excluded from the backup:", writer.name_.c_str());
				ft.WriteLine(L"  (the top-level non-selectable component '%s' is an excluded component)",
					component.fullPath_.c_str());
				writer.isExcluded_ = true;
				break;
			}
		}
	}
}

void VssClient::DiscoverExplicitelyIncludedComponents()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"Discover explicitly included components ...");

	// Enumerate all writers
	for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
	{
		VssWriter & writer = m_writerList[iWriter];
		if (writer.isExcluded_)
			continue;

		// Compute the roots of included components
		for (unsigned i = 0; i < writer.components_.size(); i++)
		{
			VssComponent & component = writer.components_[i];

			if (!component.CanBeExplicitlyIncluded())
				continue;

			// Test if our component has a parent that is also included
			component.isExplicitlyIncluded_ = true;
			for (unsigned j = 0; j < writer.components_.size(); j++)
			{
				VssComponent & ancestor = writer.components_[j];
				if (ancestor.IsAncestorOf(component) && ancestor.CanBeExplicitlyIncluded())
				{
					// This cannot be explicitely included since we have another 
					// ancestor that that must be (implictely or explicitely) included
					component.isExplicitlyIncluded_ = false;
					break;
				}
			}
		}
	}
}

void VssClient::SelectExplicitelyIncludedComponents()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"Select explicitly included components ...");

	// Enumerate all writers
	for (unsigned iWriter = 0; iWriter < m_writerList.size(); iWriter++)
	{
		VssWriter & writer = m_writerList[iWriter];
		if (writer.isExcluded_)
			continue;

		ft.WriteLine(L" * Writer '%s':", writer.name_.c_str());

		// Compute the roots of included components
		for (unsigned i = 0; i < writer.components_.size(); i++)
		{
			VssComponent & component = writer.components_[i];

			if (!component.isExplicitlyIncluded_)
				continue;

			ft.WriteLine(L"   - Add component %s", component.fullPath_.c_str());

			// Add the component
			CHECK_COM(m_pVssObject->AddComponent(
				WString2Guid(writer.instanceId_),
				WString2Guid(writer.id_),
				component.type_,
				component.logicalPath_.c_str(),
				component.name_.c_str()));
		}
	}
}

void VssClient::SelectComponentsForBackup(
	vector<wstring> shadowSourceVolumes,
	vector<wstring> excludedWriterAndComponentList,
	vector<wstring> includedWriterAndComponentList
	)
{
	FunctionTracer ft(DBG_INFO);

	// First, exclude all components that have data outside of the shadow set
	DiscoverDirectlyExcludedComponents(excludedWriterAndComponentList, m_writerList);

	// Then discover excluded components that have file groups outside the shadow set
	DiscoverNonShadowedExcludedComponents(shadowSourceVolumes);

	// Now, exclude all componenets that are have directly excluded descendents
	DiscoverAllExcludedComponents();

	// Next, exclude all writers that:
	// - either have a top-level nonselectable excluded component
	// - or do not have any included components (all its components are excluded)
	DiscoverExcludedWriters();

	// Now, discover the components that should be included (explicitly or implicitly)
	// These are the top components that do not have any excluded children
	DiscoverExplicitelyIncludedComponents();

	// Verify if the specified writers/components were included
	ft.WriteLine(L"Verifying explicitly specified writers/components ...");

	// Finally, select the explicitly included components
	SelectExplicitelyIncludedComponents();
}

int VssClient::vol2raw()
{
	FunctionTracer ft(DBG_INFO);
	Copy copy;
	for (unsigned i = 0; i < m_latestSnapshotIdList.size(); i++)
	{
		wstring snapshotID = Guid2WString(m_latestSnapshotIdList[i]);
		VSS_SNAPSHOT_PROP Snap;
		CHECK_COM(m_pVssObject->GetSnapshotProperties(WString2Guid(snapshotID), &Snap));
		wstring deviceObject = Snap.m_pwszSnapshotDeviceObject;
		wstring volumeName = Snap.m_pwszOriginalVolumeName;
		wstring volumePrefix = L"\\\\?\\Volume";
		volumeName = volumeName.substr(volumePrefix.size(), volumeName.length());
		if (volumeName[volumeName.length() - 1] == L'\\')
		{
			volumeName[volumeName.length() - 1] = L'\0';
		}
		copy.vol2raw(deviceObject, volumeName);
	}
	return 0;
}

// Pre-restore 
void VssClient::PreRestore()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"\nSending the PreRestore event ... \n");

	// Gathers writer status
	// WARNING: GatherWriterMetadata must be called before
	CComPtr<IVssAsync>  pAsync;
	CHECK_COM(m_pVssObject->PreRestore(&pAsync));

	// Waits for the async operation to finish and checks the result
	WaitAndCheckForAsyncOperation(pAsync);
}

// Post-restore 
void VssClient::PostRestore()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"\nSending the PostRestore event ... \n");

	// Gathers writer status
	// WARNING: GatherWriterMetadata must be called before
	CComPtr<IVssAsync>  pAsync;
	CHECK_COM(m_pVssObject->PostRestore(&pAsync));

	// Waits for the async operation to finish and checks the result
	WaitAndCheckForAsyncOperation(pAsync);
}

void VssClient::GatherWriterMetadataToScreen()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"(Gathering writer metadata...)");

	// Gathers writer metadata
	// WARNING: this call can be performed only once per IVssBackupComponents instance!
	CComPtr<IVssAsync>  pAsync;
	CHECK_COM(m_pVssObject->GatherWriterMetadata(&pAsync));

	// Waits for the async operation to finish and checks the result
	WaitAndCheckForAsyncOperation(pAsync);

	// Get the list of writers in the metadata  
	unsigned cWriters = 0;
	CHECK_COM(m_pVssObject->GetWriterMetadataCount(&cWriters));

	// Enumerate writers
	for (unsigned iWriter = 0; iWriter < cWriters; iWriter++)
	{
		CComBSTR bstrXML;
		// Get the metadata for this particular writer
		VSS_ID idInstance = GUID_NULL;
		CComPtr<IVssExamineWriterMetadata> pMetadata;
		CHECK_COM(m_pVssObject->GetWriterMetadata(iWriter, &idInstance, &pMetadata));

		CHECK_COM(pMetadata->SaveAsXML(&bstrXML));
		wprintf(L"\n--[Writer %u]--\n%s\n", iWriter, (LPCWSTR)(bstrXML));
	}

	wprintf(L"--[end of data]--\n");
}

void VssClient::SetAsrRestoreStatus(bool status)
{

	//FunctionTracer ft(DBG_INFO);

	/*
	FunctionTracer ft(DBG_INFO);
	VSS_FILE_RESTORE_STATUS restoreStatus = status ? VSS_RS_ALL : VSS_RS_NONE;
	unsigned cWriters = 0;
	CHECK_COM(m_pVssObject->GetWriterComponentsCount(&cWriters));
	ft.WriteLine(L"\nSetAsrRestoreStatus, writers %d\n", cWriters);
	for (unsigned iWriter = 0; iWriter < cWriters; iWriter++)
	{
		// Get the selected components for this particular writer
		CComPtr<IVssWriterComponentsExt> pWriterComponents;
		CHECK_COM(m_pVssObject->GetWriterComponents(iWriter, &pWriterComponents));

		VSS_ID idInstance = GUID_NULL;
		VSS_ID idWriter = GUID_NULL;
		CHECK_COM(pWriterComponents->GetWriterInfo(
			&idInstance,
			&idWriter
		));
		wstring id = Guid2WString(idWriter);
		wstring instanceId = Guid2WString(idInstance);
		unsigned cComponents = 0;
		CHECK_COM(pWriterComponents->GetComponentCount(&cComponents));
		for (unsigned iComponent = 0; iComponent < cComponents; iComponent++)
		{
			// Get component
			CComPtr<IVssComponent> pComponent;
			CHECK_COM(pWriterComponents->GetComponent(iComponent, &pComponent));
			CComBSTR bstrLogicalPath;
			CHECK_COM(pComponent->GetLogicalPath(&bstrLogicalPath));
			// Get component type
			VSS_COMPONENT_TYPE  type;
			CHECK_COM(pComponent->GetComponentType(&type));
			// Get component name
			CComBSTR bstrComponentName;
			CHECK_COM(pComponent->GetComponentName(&bstrComponentName));

			CHECK_COM(m_pVssObject->SetFileRestoreStatus(
				idWriter,
				type,
				bstrLogicalPath,
				bstrComponentName,
				restoreStatus)
			);
		}
	}
	*/

	
	FunctionTracer ft(DBG_INFO);

	VSS_FILE_RESTORE_STATUS restoreStatus = status ? VSS_RS_ALL : VSS_RS_NONE;
	//wstring asrDoc = ReadFileContents(ASR_WRITER_XML);
	//CComPtr<IVssExamineWriterMetadata> pMetadata;

	//CHECK_COM(CreateVssExamineWriterMetadata(CComBSTR(asrDoc.c_str()), &pMetadata));

	// Get writer identity
	VSS_ID idInstance = GUID_NULL;
	VSS_ID idWriter = GUID_NULL;
	//CComBSTR bstrWriterName;
	//VSS_USAGE_TYPE usage = VSS_UT_UNDEFINED;
	//VSS_SOURCE_TYPE source = VSS_ST_UNDEFINED;
	//CHECK_COM(pMetadata->GetIdentity(
	//	&idInstance,
	//	&idWriter,
	//	&bstrWriterName,
	//	&usage,
	//	&source
	//));

	//// Get file counts
	//unsigned cIncludeFiles = 0;
	//unsigned cExcludeFiles = 0;
	//unsigned cComponents = 0;
	//ft.WriteLine(L"SetAsrRestoreStatus, writer name %s", bstrWriterName);
	wstring asr_writer = L"{be000cbe-11fe-4426-9c58-531aa6355fc4}";
	idWriter = WString2Guid(asr_writer);
	CHECK_COM(m_pVssObject->SetRestoreOptions(
		idWriter,
		VSS_CT_FILEGROUP,
		L"ASR",
		L"ASR",
		TEXT("\"IncludeDisk\"=\"0\", \"ExcludeDisk\"=\"1\" "))
	);
	ft.WriteLine(L"===SetAsrRestoreStatus, SetRestoreOptions ok");
	//CHECK_COM(m_pVssObject->SetSelectedForRestore(
	//	idWriter,
	//	VSS_CT_FILEGROUP,
	//	L"ASR",
	//	L"ASR",
	//	true));
	//ft.WriteLine(L"===ASR, ASR selected");
	/*CHECK_COM(m_pVssObject->SetSelectedForRestore(
		idWriter,
		VSS_CT_FILEGROUP,
		L"Volumes",
		L"Volume{45ed89a9-2997-11eb-80b3-806e6f6e6963}",
		true));
	ft.WriteLine(L"===Volumes, Volume{45ed89a9-2997-11eb-80b3-806e6f6e6963} selected");
	CHECK_COM(m_pVssObject->SetSelectedForRestore(
		idWriter,
		VSS_CT_FILEGROUP,
		L"Volumes",
		L"Volume{45ed89aa-2997-11eb-80b3-806e6f6e6963}",
		true));
	ft.WriteLine(L"===Volumes, Volume{45ed89aa-2997-11eb-80b3-806e6f6e6963} selected");
	CHECK_COM(m_pVssObject->SetSelectedForRestore(
		idWriter,
		VSS_CT_FILEGROUP,
		L"Disks",
		L"harddisk0",
		true));
	ft.WriteLine(L"===Disks, harddisk0 selected");
	CHECK_COM(m_pVssObject->SetSelectedForRestore(
		idWriter,
		VSS_CT_FILEGROUP,
		L"BCD",
		L"BCD",
		true));
	ft.WriteLine(L"===BCD, BCD selected");*/

	//CHECK_COM(m_pVssObject->SetFileRestoreStatus(
	//	idWriter,
	//	VSS_CT_FILEGROUP,
	//	L"ASR",
	//	L"ASR",
	//	restoreStatus)
	//);
	/*
	CHECK_COM(pMetadata->GetFileCounts(&cIncludeFiles, &cExcludeFiles, &cComponents));
	for (unsigned iComponent = 0; iComponent < cComponents; iComponent++)
	{
		// Get component
		CComPtr<IVssWMComponent> pComponent;
		CHECK_COM(pMetadata->GetComponent(iComponent, &pComponent));
		// Get the component info
		PVSSCOMPONENTINFO pInfo = NULL;
		CHECK_COM(pComponent->GetComponentInfo(&pInfo));
		CHECK_COM(m_pVssObject->SetFileRestoreStatus(
			idWriter,
			pInfo->type,
			pInfo->bstrLogicalPath,
			pInfo->bstrComponentName,
			restoreStatus)
		);
	}
	*/
	
}

wstring gen_random(const int len) {

	wstring tmp_s;
	static const WCHAR alphanum[] =
		L"0123456789"
		L"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		L"abcdefghijklmnopqrstuvwxyz";

	srand((unsigned)time(NULL));

	for (int i = 0; i < len; ++i)
		tmp_s += alphanum[rand() % (wcslen(alphanum) - 1)];

	return tmp_s;

}

void VssClient::Registrykey(vector<wstring> &vols)
{
	FunctionTracer ft(DBG_INFO);
	HKEY hKey;
	HKEY hLastInstance;
	wstring path(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ASR\\RestoreSession");

	DWORD dwDisposition = REG_OPENED_EXISTING_KEY;

	LONG ret = RegCreateKeyEx(
		HKEY_LOCAL_MACHINE,
		path.c_str(),
		0,
		NULL,
		REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
		NULL,
		&hKey,
		&dwDisposition);
	if (ret != ERROR_SUCCESS)
	{
		ft.WriteLine(L"create RestoreSession failed ");
		return;
	}
	// regset LastInstance 
	//wstring uuid = gen_random(24);
	wstring uuid = L"0b8f10f6-8767-4bef-9735-dadcfcaa1f9f";
	ret = RegSetValueEx(hKey, L"LastInstance", 0, REG_SZ, (BYTE*)uuid.c_str(), uuid.length()*sizeof(WCHAR));
	if (ret != ERROR_SUCCESS)
	{
		ft.WriteLine(L"set LastInstance failed ");
		return;
	}
	
	wstring restorevolumes;
	for (vector<wstring>::iterator iter = vols.begin(); iter != vols.end(); ++iter)
	{
		restorevolumes += L"\\\\?\\Volume" + *iter;
		restorevolumes += L'\0';
	}

	ret = RegSetValueEx(hKey, L"RestoredVolumes ", 0, REG_MULTI_SZ, (BYTE*)restorevolumes.c_str(), restorevolumes.length()*sizeof(WCHAR));
	//ret = RegSetValueEx(hKey, L"RestoredVolumes ", 0, REG_MULTI_SZ, (BYTE*)restorevolumes.c_str(), 0);
	if (ret != ERROR_SUCCESS)
	{
		ft.WriteLine(L"set RestoredVolumes failed ");
		return;
	}
	RegCloseKey(hKey);
	ft.WriteLine(L"set RestoreSession success ");
	return;
}

void VssClient::SaveAsrXml()
{
	FunctionTracer ft(DBG_INFO);
	unsigned cWriters = 0;
	CHECK_COM(m_pVssObject->GetWriterMetadataCount(&cWriters));

	// Enumerate writers
	for (unsigned iWriter = 0; iWriter < cWriters; iWriter++)
	{
		// Get the metadata for this particular writer
		VSS_ID idInstance = GUID_NULL;
		CComPtr<IVssExamineWriterMetadata> pMetadata;
		CHECK_COM(m_pVssObject->GetWriterMetadata(iWriter, &idInstance, &pMetadata));

		VssWriter   writer;
		writer.Initialize(pMetadata);

		// Add this writer to the list 
		if (writer.name_ == L"ASR Writer")
		{
			CComBSTR bstrXML;
			CHECK_COM(pMetadata->SaveAsXML(&bstrXML));
			WriteFile(ASR_WRITER_XML, BSTR2WString(bstrXML));
		}
	}
}

void VssClient::SetRestoreOptions()
{
	FunctionTracer ft(DBG_INFO);
	VSS_ID idInstance = GUID_NULL;
	VSS_ID idWriter = GUID_NULL;
	wstring asr_writer = L"{be000cbe-11fe-4426-9c58-531aa6355fc4}";
	idWriter = WString2Guid(asr_writer);
	CHECK_COM(m_pVssObject->SetRestoreOptions(
		idWriter,
		VSS_CT_FILEGROUP,
		L"ASR",
		L"ASR",
		TEXT("\"IncludeDisk\"=\"0\", \"ExcludeDisk\"=\"1\" "))
	);
	ft.WriteLine(L"===SetRestoreOptions, SetRestoreOptions ok");
}