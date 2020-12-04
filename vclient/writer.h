#pragma once
#include "stdafx.h"
#include <vswriter.h>
#include <vsbackup.h>

// The type of a file descriptor
typedef enum
{
	VSS_FDT_UNDEFINED = 0,
	VSS_FDT_EXCLUDE_FILES,
	VSS_FDT_FILELIST,
	VSS_FDT_DATABASE,
	VSS_FDT_DATABASE_LOG,
} VSS_DESCRIPTOR_TYPE;

struct VssFileDescriptor
{
	VssFileDescriptor() :
		isRecursive_(false),
		type_(VSS_FDT_UNDEFINED)
	{};
	// Initialize from a IVssWMFiledesc
	void Initialize(
		IVssWMFiledesc * pFileDesc,
		VSS_DESCRIPTOR_TYPE typeParam
		);

	// Print this file descriptor 
	void Print();

	// Get the string representation of the type
	wstring GetStringFromFileDescriptorType(VSS_DESCRIPTOR_TYPE eType);

	wstring             path_;
	wstring             filespec_;
	wstring             alternatePath_;
	bool                isRecursive_;

	VSS_DESCRIPTOR_TYPE type_;
	wstring             expandedPath_;
	wstring             affectedVolume_;
};

struct VssDependency
{
	VssDependency() {};

	// Initialize from a IVssWMDependency
	void Initialize(
		IVssWMDependency * pDependency
		);

	// Print this dependency
	void Print();

	//
	//  Data members
	//

	wstring             writerId_;
	wstring             logicalPath_;
	wstring             componentName_;
	wstring             fullPath_;
};

struct VssComponent
{
	VssComponent() :
		type_(VSS_CT_UNDEFINED),
		isSelectable_(false),
		notifyOnBackupComplete_(false),
		isTopLevel_(false),
		isExcluded_(false),
		isExplicitlyIncluded_(false)
	{};

	// Initialize from a IVssWMComponent
	void Initialize(wstring writerNameParam, IVssWMComponent * pComponent);

	// Initialize from a IVssComponent
	void Initialize(wstring writerNameParam, IVssComponent * pComponent);

	// Print summary/detalied information about this component
	void Print(bool bListDetailedInfo);

	// Convert a component type into a string
	wstring GetStringFromComponentType(VSS_COMPONENT_TYPE eComponentType);

	// Return TRUE if the current component is ancestor of the given component
	bool IsAncestorOf(VssComponent & child);

	// return TRUEif it can be explicitly included
	bool CanBeExplicitlyIncluded();

	//
	//  Data members
	//

	wstring             name_;
	wstring             writerName_;
	wstring             logicalPath_;
	wstring             caption_;
	VSS_COMPONENT_TYPE  type_;
	bool                isSelectable_;
	bool                notifyOnBackupComplete_;

	wstring             fullPath_;
	bool                isTopLevel_;
	bool                isExcluded_;
	bool                isExplicitlyIncluded_;
	vector<wstring>     affectedPaths_;
	vector<wstring>     affectedVolumes_;
	vector<VssFileDescriptor> descriptors_;
	vector<VssDependency> dependencies_;
};

struct VssWriter
{
	VssWriter() :
		isExcluded_(false),
		supportsRestore_(false),
		restoreMethod_(VSS_RME_UNDEFINED),
		writerRestoreConditions_(VSS_WRE_UNDEFINED),
		rebootRequiredAfterRestore_(false)
	{};

	// Initialize from a IVssWMFiledesc
	void Initialize(IVssExamineWriterMetadata * pMetadata);

	// Print summary/detalied information about this writer
	void Print(bool bListDetailedInfo);

	wstring GetStringFromRestoreMethod(VSS_RESTOREMETHOD_ENUM eRestoreMethod);

	wstring GetStringFromRestoreConditions(VSS_WRITERRESTORE_ENUM eRestoreEnum);

	//
	//  Data members
	//

	wstring                     name_;
	wstring                     id_;
	wstring                     instanceId_;
	vector<VssComponent>        components_;
	vector<VssFileDescriptor>   excludedFiles_;
	VSS_WRITERRESTORE_ENUM      writerRestoreConditions_;
	bool                        supportsRestore_;
	VSS_RESTOREMETHOD_ENUM      restoreMethod_;
	bool                        rebootRequiredAfterRestore_;
	bool                        isExcluded_;
};