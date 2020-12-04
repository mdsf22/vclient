// stdafx.h : ��׼ϵͳ�����ļ��İ����ļ���
// ���Ǿ���ʹ�õ��������ĵ�
// �ض�����Ŀ�İ����ļ�
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

// TODO:  �ڴ˴����ó�����Ҫ������ͷ�ļ�
