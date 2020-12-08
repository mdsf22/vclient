#pragma once
#include "stdafx.h"

class VssClient
{
public:

	// Constructor
	VssClient();

	// Destructor
	~VssClient();

	// Initialize the internal pointers
	void Initialize(DWORD dwContext = VSS_CTX_BACKUP, wstring xmlDoc = L"", bool bDuringRestore = false);

	//
	//  Shadow copy creation related methods
	//

	// Method to create a shadow copy set with the given volumes
	void CreateSnapshotSet(
		vector<wstring> volumeList,
		wstring outputXmlFile,
		vector<wstring> excludedWriterList,
		vector<wstring> includedWriterList
		);

	// Prepare the shadow copy for backup
	void PrepareForBackup();

	// Add volumes to the shadow copy set
	void AddToSnapshotSet(vector<wstring> volumeList);

	// Effectively creating the shadow copy (calling DoSnapshotSet)
	void DoSnapshotSet();

	// Ending the backup (calling BackupComplete)
	void BackupComplete(bool succeeded);

	// Save the backup components document
	void SaveBackupComponentsDocument(wstring fileName);

	// Gather writer metadata
	void GatherWriterMetadata();

	// Initialize writer metadata
	void InitializeWriterMetadata();

	// Discover directly excluded components (that were excluded through the command-line)
	void DiscoverDirectlyExcludedComponents(
		vector<wstring> excludedWriterAndComponentList,
		vector<VssWriter> & writerList
		);

	// Discover excluded components that have file groups outside the shadow set
	void DiscoverNonShadowedExcludedComponents(
		vector<wstring> shadowSourceVolumes
		);

	// Discover the components that should not be included (explicitly or implicitly)
	// These are componenets that are have directly excluded descendents
	void DiscoverAllExcludedComponents();

	// Discover excluded writers. These are writers that:
	// - either have a top-level nonselectable excluded component
	// - or do not have any included components (all its components are excluded)
	void DiscoverExcludedWriters();

	// Discover the components that should be explicitly included 
	// These are any included top components 
	void DiscoverExplicitelyIncludedComponents();

	// Select explicitly included components
	void SelectExplicitelyIncludedComponents();

	void SelectComponentsForBackup(
		vector<wstring> shadowSourceVolumes,
		vector<wstring> excludedWriterAndComponentList,
		vector<wstring> includedWriterAndComponentList
		);

	// Query all the shadow copies in the given set
	// If snapshotSetID is NULL, just query all shadow copies in the system
	void QuerySnapshotSet(VSS_ID snapshotSetID);

	// Print the properties for the given snasphot
	void PrintSnapshotProperties(VSS_SNAPSHOT_PROP & prop);

	// Check the status for all selected writers
	void CheckSelectedWriterStatus();

	void GatherWriterStatus();

	bool IsWriterSelected(GUID guidInstanceId);

	// Get writer status as string
	wstring GetStringFromWriterStatus(VSS_WRITER_STATE eWriterStatus);

	// Marks all selected components as succeeded for backup
	void SetBackupSucceeded(bool succeeded);

	int vol2raw();

	void PreRestore();

	void PostRestore();

	void GatherWriterMetadataToScreen();

	void SetAsrRestoreStatus(bool status);

	void Registrykey(vector<wstring> &vols);

	void SaveAsrXml();

	void SetRestoreOptions();
private:

	// Waits for the async operation to finish
	void WaitAndCheckForAsyncOperation(IVssAsync*  pAsync);
private:

	//
	//  Data members
	//

	// TRUE if CoInitialize() was already called 
	// Needed to pair each succesfull call to CoInitialize with a corresponding CoUninitialize
	bool                            m_bCoInitializeCalled;

	// VSS context
	DWORD                           m_dwContext;

	// The IVssBackupComponents interface is automatically released when this object is destructed.
	// Needed to issue VSS calls 
	CComPtr<IVssBackupComponents>   m_pVssObject;

	// List of selected writers during the shadow copy creation process
	vector<wstring>                 m_latestVolumeList;

	// List of shadow copy IDs from the latest shadow copy creation process
	vector<VSS_ID>                  m_latestSnapshotIdList;

	// Latest shadow copy set ID
	VSS_ID                          m_latestSnapshotSetID;

	// List of writers
	vector<VssWriter>               m_writerList;

	// List of selected writers/componnets from the previous backup components document
	vector<VssWriter>               m_writerComponentsForRestore;

	// List of resync pairs
	map<VSS_ID, wstring, ltguid>      m_resyncPairs;

	// TRUE if we are during restore
	bool                            m_bDuringRestore;
};