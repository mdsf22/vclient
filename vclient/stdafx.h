// stdafx.h : 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 特定于项目的包含文件
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>

// General includes
#include <windows.h>
#include <winbase.h>

#include "macros.h"

// ATL includes
#pragma warning( disable: 4189 )    // disable local variable is initialized but not referenced
#include <atlbase.h>

// VSS includes
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include <vsmgmt.h>

// STL includes
#include <vector>
#include <map>
#include <algorithm>
#include <string>
#include <fstream>
using namespace std;

#include "tracing.h"
#include "util.h"
#include "writer.h"
#include "vssclient.h"
#include "copy.h"

// Used for safe string manipulation
#include <strsafe.h>

// TODO:  在此处引用程序需要的其他头文件
