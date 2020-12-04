#include "stdafx.h"


void VssWriter::Initialize(IVssExamineWriterMetadata * pMetadata)
{
	FunctionTracer ft(DBG_INFO);

	// Get writer identity information
	VSS_ID idInstance = GUID_NULL;
	VSS_ID idWriter = GUID_NULL;
	CComBSTR bstrWriterName;
	VSS_USAGE_TYPE usage = VSS_UT_UNDEFINED;
	VSS_SOURCE_TYPE source = VSS_ST_UNDEFINED;
	CComBSTR bstrService;
	CComBSTR bstrUserProcedure;
	UINT iMappings;

	// Get writer identity
	CHECK_COM(pMetadata->GetIdentity(
		&idInstance,
		&idWriter,
		&bstrWriterName,
		&usage,
		&source
		));

	// Get the restore method 
	CHECK_COM(pMetadata->GetRestoreMethod(
		&restoreMethod_,
		&bstrService,
		&bstrUserProcedure,
		&writerRestoreConditions_,
		&rebootRequiredAfterRestore_,
		&iMappings
		));

	// Initialize local members
	name_ = (LPWSTR)bstrWriterName;
	id_ = Guid2WString(idWriter);
	instanceId_ = Guid2WString(idInstance);
	supportsRestore_ = (writerRestoreConditions_ != VSS_WRE_NEVER);

	// Get file counts
	unsigned cIncludeFiles = 0;
	unsigned cExcludeFiles = 0;
	unsigned cComponents = 0;
	CHECK_COM(pMetadata->GetFileCounts(&cIncludeFiles, &cExcludeFiles, &cComponents));


	// Enumerate components
	for (unsigned iComponent = 0; iComponent < cComponents; iComponent++)
	{
		// Get component
		CComPtr<IVssWMComponent> pComponent;
		CHECK_COM(pMetadata->GetComponent(iComponent, &pComponent));

		// Add this component to the list of components
		VssComponent component;
		component.Initialize(name_, pComponent);
		components_.push_back(component);
	}

	// Discover toplevel components
	for (unsigned i = 0; i < cComponents; i++)
	{
		components_[i].isTopLevel_ = true;
		for (unsigned j = 0; j < cComponents; j++)
			if (components_[j].IsAncestorOf(components_[i]))
				components_[i].isTopLevel_ = false;
	}
	if (name_ == L"ASR Writer")
	{
		for (unsigned i = 0; i < cComponents; i++)
		{
			if (components_[i].logicalPath_ == L"Volumes")
				components_[i].isExcluded_ = components_[i].isSelectable_;
		}
	}
}

void VssWriter::Print(bool bListDetailedInfo)
{
	FunctionTracer ft(DBG_INFO);

	// Print writer identity information
	ft.WriteLine(L"\n"
		L"* WRITER \"%s\"\n"
		L"    - WriterId   = %s\n"
		L"    - InstanceId = %s\n"
		L"    - Supports restore events = %s\n"
		L"    - Writer restore conditions = %s\n"
		L"    - Restore method = %s\n"
		L"    - Requires reboot after restore = %s\n",
		name_.c_str(),
		id_.c_str(),
		instanceId_.c_str(),
		BOOL2TXT(supportsRestore_),
		GetStringFromRestoreConditions(writerRestoreConditions_).c_str(),
		GetStringFromRestoreMethod(restoreMethod_).c_str(),
		BOOL2TXT(rebootRequiredAfterRestore_)
		);
	/*
	// Print exclude files
	ft.WriteLine(L"    - Excluded files:");
	for (unsigned i = 0; i < excludedFiles_.size(); i++)
		excludedFiles_[i].Print();

	// Enumerate components
	for (unsigned i = 0; i < components_.size(); i++)
		components_[i].Print(bListDetailedInfo);
	*/
}

// Convert a component type into a string
inline wstring VssWriter::GetStringFromRestoreMethod(VSS_RESTOREMETHOD_ENUM eRestoreMethod)
{
	FunctionTracer ft(DBG_INFO);

	ft.Trace(DBG_INFO, L"Interpreting constant %d", (int)eRestoreMethod);
	switch (eRestoreMethod)
	{
		CHECK_CASE_FOR_CONSTANT(VSS_RME_UNDEFINED);
		CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_IF_NOT_THERE);
		CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_IF_CAN_REPLACE);
		CHECK_CASE_FOR_CONSTANT(VSS_RME_STOP_RESTORE_START);
		CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_TO_ALTERNATE_LOCATION);
		CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_AT_REBOOT);
#ifdef VSS_SERVER
		CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_AT_REBOOT_IF_CANNOT_REPLACE);
#endif
		CHECK_CASE_FOR_CONSTANT(VSS_RME_CUSTOM);
		CHECK_CASE_FOR_CONSTANT(VSS_RME_RESTORE_STOP_START);

	default:
		ft.WriteLine(L"Unknown constant: %d", eRestoreMethod);
		_ASSERTE(false);
		return wstring(L"Undefined");
	}
}


// Convert a component type into a string
inline wstring VssWriter::GetStringFromRestoreConditions(VSS_WRITERRESTORE_ENUM eRestoreEnum)
{
	FunctionTracer ft(DBG_INFO);

	ft.Trace(DBG_INFO, L"Interpreting constant %d", (int)eRestoreEnum);
	switch (eRestoreEnum)
	{
		CHECK_CASE_FOR_CONSTANT(VSS_WRE_UNDEFINED);
		CHECK_CASE_FOR_CONSTANT(VSS_WRE_NEVER);
		CHECK_CASE_FOR_CONSTANT(VSS_WRE_IF_REPLACE_FAILS);
		CHECK_CASE_FOR_CONSTANT(VSS_WRE_ALWAYS);

	default:
		ft.WriteLine(L"Unknown constant: %d", eRestoreEnum);
		_ASSERTE(false);
		return wstring(L"Undefined");
	}
}

// Initialize from a IVssWMComponent
void VssComponent::Initialize(wstring writerNameParam, IVssWMComponent * pComponent)
{
	FunctionTracer ft(DBG_INFO);

	writerName_ = writerNameParam;

	// Get the component info
	PVSSCOMPONENTINFO pInfo = NULL;
	CHECK_COM(pComponent->GetComponentInfo(&pInfo));

	// Initialize local members
	name_ = BSTR2WString(pInfo->bstrComponentName);
	logicalPath_ = BSTR2WString(pInfo->bstrLogicalPath);
	caption_ = BSTR2WString(pInfo->bstrCaption);
	type_ = pInfo->type;
	isSelectable_ = pInfo->bSelectable;
	notifyOnBackupComplete_ = pInfo->bNotifyOnBackupComplete;

	// Compute the full path
	fullPath_ = AppendBackslash(logicalPath_) + name_;
	if (fullPath_[0] != L'\\')
		fullPath_ = wstring(L"\\") + fullPath_;

	// Get file list descriptors
	for (unsigned i = 0; i < pInfo->cFileCount; i++)
	{
		CComPtr<IVssWMFiledesc> pFileDesc;
		CHECK_COM(pComponent->GetFile(i, &pFileDesc));

		VssFileDescriptor desc;
		desc.Initialize(pFileDesc, VSS_FDT_FILELIST);
		descriptors_.push_back(desc);
	}

	// Get database descriptors
	for (unsigned i = 0; i < pInfo->cDatabases; i++)
	{
		CComPtr<IVssWMFiledesc> pFileDesc;
		CHECK_COM(pComponent->GetDatabaseFile(i, &pFileDesc));

		VssFileDescriptor desc;
		desc.Initialize(pFileDesc, VSS_FDT_DATABASE);
		descriptors_.push_back(desc);
	}

	// Get log descriptors
	for (unsigned i = 0; i < pInfo->cLogFiles; i++)
	{
		CComPtr<IVssWMFiledesc> pFileDesc;
		CHECK_COM(pComponent->GetDatabaseLogFile(i, &pFileDesc));

		VssFileDescriptor desc;
		desc.Initialize(pFileDesc, VSS_FDT_DATABASE_LOG);
		descriptors_.push_back(desc);
	}

	// Get dependencies
	for (unsigned i = 0; i < pInfo->cDependencies; i++)
	{
		CComPtr<IVssWMDependency> pDependency;
		CHECK_COM(pComponent->GetDependency(i, &pDependency));

		VssDependency dependency;
		dependency.Initialize(pDependency);
		dependencies_.push_back(dependency);
	}


	pComponent->FreeComponentInfo(pInfo);

	// Compute the affected paths and volumes
	for (unsigned i = 0; i < descriptors_.size(); i++)
	{
		if (!FindStringInList(descriptors_[i].expandedPath_, affectedPaths_))
			affectedPaths_.push_back(descriptors_[i].expandedPath_);

		if (!FindStringInList(descriptors_[i].affectedVolume_, affectedVolumes_))
			affectedVolumes_.push_back(descriptors_[i].affectedVolume_);
	}


	sort(affectedPaths_.begin(), affectedPaths_.end());
}


// Initialize from a IVssComponent
void VssComponent::Initialize(wstring writerNameParam, IVssComponent * pComponent)
{
	FunctionTracer ft(DBG_INFO);

	writerName_ = writerNameParam;

	// Get component type
	CHECK_COM(pComponent->GetComponentType(&type_));

	// Get component name
	CComBSTR bstrComponentName;
	CHECK_COM(pComponent->GetComponentName(&bstrComponentName));
	name_ = BSTR2WString(bstrComponentName);

	// Get component logical path
	CComBSTR bstrLogicalPath;
	CHECK_COM(pComponent->GetLogicalPath(&bstrLogicalPath));
	logicalPath_ = BSTR2WString(bstrLogicalPath);

	// Compute the full path
	fullPath_ = AppendBackslash(logicalPath_) + name_;
	if (fullPath_[0] != L'\\')
		fullPath_ = wstring(L"\\") + fullPath_;
}

// Convert a component type into a string
inline wstring VssComponent::GetStringFromComponentType(VSS_COMPONENT_TYPE eComponentType)
{
	FunctionTracer ft(DBG_INFO);

	ft.Trace(DBG_INFO, L"Interpreting constant %d", (int)eComponentType);
	switch (eComponentType)
	{
		CHECK_CASE_FOR_CONSTANT(VSS_CT_DATABASE);
		CHECK_CASE_FOR_CONSTANT(VSS_CT_FILEGROUP);

	default:
		ft.WriteLine(L"Unknown constant: %d", eComponentType);
		_ASSERTE(false);
		return wstring(L"Undefined");
	}
}


// Return TRUE if the current component is parent of the given component
bool VssComponent::IsAncestorOf(VssComponent & descendent)
{
	// The child must have a longer full path
	if (descendent.fullPath_.length() <= fullPath_.length())
		return false;

	wstring fullPathAppendedWithBackslash = AppendBackslash(fullPath_);
	wstring descendentPathAppendedWithBackslash = AppendBackslash(descendent.fullPath_);

	// Return TRUE if the current full path is a prefix of the child full path
	return IsEqual(fullPathAppendedWithBackslash,
		descendentPathAppendedWithBackslash.substr(0,
			fullPathAppendedWithBackslash.length()));
}

bool VssComponent::CanBeExplicitlyIncluded()
{
	if (isExcluded_)
		return false;

	// selectable can be explictly included
	if (isSelectable_)
		return true;

	// Non-selectable top level can be explictly included
	if (isTopLevel_)
		return true;

	return false;
}


//////////////////////////////////////////////////////////////////////
void VssFileDescriptor::Initialize(
	IVssWMFiledesc * pFileDesc,
	VSS_DESCRIPTOR_TYPE typeParam
	)
{
	FunctionTracer ft(DBG_INFO);

	// Set the type
	type_ = typeParam;

	CComBSTR bstrPath;
	CHECK_COM(pFileDesc->GetPath(&bstrPath));

	CComBSTR bstrFilespec;
	CHECK_COM(pFileDesc->GetFilespec(&bstrFilespec));

	bool bRecursive = false;
	CHECK_COM(pFileDesc->GetRecursive(&bRecursive));

	CComBSTR bstrAlternate;
	CHECK_COM(pFileDesc->GetAlternateLocation(&bstrAlternate));

	// Initialize local data members
	path_ = BSTR2WString(bstrPath);
	filespec_ = BSTR2WString(bstrFilespec);
	expandedPath_ = bRecursive;
	path_ = BSTR2WString(bstrPath);

	// Get the expanded path
	expandedPath_.resize(MAX_PATH, L'\0');
	_ASSERTE(bstrPath && bstrPath[0]);
	CHECK_WIN32(ExpandEnvironmentStringsW(bstrPath, (PWCHAR)expandedPath_.c_str(), (DWORD)expandedPath_.length()));
	expandedPath_ = AppendBackslash(expandedPath_);

	// Get the affected volume 
	if (!GetUniqueVolumeNameForPathNoThrow(expandedPath_, affectedVolume_))
	{
		affectedVolume_ = expandedPath_;
	}

}


// Print a file description object
inline void VssFileDescriptor::Print()
{
	FunctionTracer ft(DBG_INFO);

	wstring alternateDisplayPath;
	if (alternatePath_.length() > 0)
		alternateDisplayPath = wstring(L", Alternate Location = ") + alternatePath_;

	ft.WriteLine(L"       - %s: Path = %s, Filespec = %s%s%s",
		GetStringFromFileDescriptorType(type_).c_str(),
		path_.c_str(),
		filespec_.c_str(),
		isRecursive_ ? L", Recursive" : L"",
		alternateDisplayPath.c_str());
}


// Convert a component type into a string
wstring VssFileDescriptor::GetStringFromFileDescriptorType(VSS_DESCRIPTOR_TYPE eType)
{
	FunctionTracer ft(DBG_INFO);

	ft.Trace(DBG_INFO, L"Interpreting constant %d", (int)eType);
	switch (eType)
	{
	case VSS_FDT_UNDEFINED:     return L"Undefined";
	case VSS_FDT_EXCLUDE_FILES: return L"Exclude";
	case VSS_FDT_FILELIST:      return L"File List";
	case VSS_FDT_DATABASE:      return L"Database";
	case VSS_FDT_DATABASE_LOG:  return L"Database Log";

	default:
		ft.WriteLine(L"Unknown constant: %d", eType);
		_ASSERTE(false);
		return wstring(L"Undefined");
	}
}

void VssDependency::Initialize(
	IVssWMDependency * pDependency
	)
{
	FunctionTracer ft(DBG_INFO);

	VSS_ID guidWriterId;
	CHECK_COM(pDependency->GetWriterId(&guidWriterId));

	CComBSTR bstrLogicalPath;
	CHECK_COM(pDependency->GetLogicalPath(&bstrLogicalPath));

	CComBSTR bstrComponentName;
	CHECK_COM(pDependency->GetComponentName(&bstrComponentName));

	// Initialize local data members
	writerId_ = Guid2WString(guidWriterId);
	logicalPath_ = BSTR2WString(bstrLogicalPath);
	componentName_ = BSTR2WString(bstrComponentName);

	// Compute the full path
	fullPath_ = AppendBackslash(logicalPath_) + componentName_;
	if (fullPath_[0] != L'\\')
		fullPath_ = wstring(L"\\") + fullPath_;
}


// Print a file description object
inline void VssDependency::Print()
{
	FunctionTracer ft(DBG_INFO);

	ft.WriteLine(L"       - Dependency to \"%s:%s%s\"",
		writerId_.c_str(),
		fullPath_.c_str());
}