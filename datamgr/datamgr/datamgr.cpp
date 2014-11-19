#pragma warning(disable:4996)
#include <Windows.h>
#include <ShlObj.h>
#include <atlbase.h>
#include <string>
#include <list>
#include <iostream>
#include <sys/stat.h>
#include <tinystr.h>
#include <tinyxml.h>
#include <json/json.h>
#include "Edoc2Context.h"
#include "datamgr.h"
#include "Proto.h"
#include "HttpImpl.h"
#include "JsonImpl.h"
#include "Utility.h"

/*
// Json request
{
	Server: "127.0.0.1",
	Port: 60684,
	Version: "1.0.0.1", 
	Method: "SampleRequest",
	Param: {param1: value1, param2: value2, ...}
}

// Json response
{
	Ack: "SampleRequest",
	statusCode: 0,
	Status: "Code is 0, so everything is ok.",
	Return: {ret1: value1, ret2: value2, ...}
}
*/


///////////////////////////////////////////////////////////////////////////////
// cache directory
#define VDRIVE_LOCAL_CACHE_ROOT ((pArchive && pArchive->context) \
								? (pArchive->context->cachedir) \
								: _T(""))

#define ENABLE_HTTP(pArchive)  (pArchive && pArchive->context && pArchive->context->enableHttp)

///////////////////////////////////////////////////////////////////////////////
// Global Context
static Edoc2Context * gspEdoc2Context = NULL;
static __inline Proto * GetProto(TAR_ARCHIVE * pArchive){ return (pArchive->context->proto);}
///////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// Global database ref-ed by id
struct ServerItemInfo
{
    DWORD id;
    BOOL  isLocked;
    BOOL  isFolder;
    DWORD dwCurPage;
    DWORD dwTotalPage;
    wchar_t szName[MAX_PATH];
    wchar_t szQuery[MAX_PATH];
};
typedef std::map<DWORD, ServerItemInfo> DBType;
static DBType * gspGlobalDB = NULL;
static __inline DBType * GetDB(TAR_ARCHIVE * pArchive) {return (gspGlobalDB);}
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// Global database index-ed by HWND
typedef std::map<HWND, WndInfo> WndDbType;
static WndDbType * gspGlobalWndDB = NULL;
static __inline WndDbType * GetWndDB(TAR_ARCHIVE * pArchive){return (gspGlobalWndDB);}
//////////////////////////////////////////////////////////////////////////

// TAR archive manipulation

/**
* Initialize from configuration file.
* e.g: libdatamgr.ini, libdatamgr.xml
*/
static HRESULT DMInit(HINSTANCE hInst){
	HRESULT hr = E_FAIL;
	if (gspEdoc2Context) return S_OK;

    // Create a global db
    gspGlobalDB = new DBType();
    // Create a global db
    gspGlobalWndDB = new WndDbType();

    // Create a global context
	gspEdoc2Context = new Edoc2Context();
	memset(gspEdoc2Context, 0, sizeof(Edoc2Context));

	struct Edoc2Context & context = *gspEdoc2Context;

    // Setup Instance handle
    context.hInst = hInst;

    // Setup module path
    GetModuleFileName(hInst, context.modulepath, lengthof(context.modulepath));
    wcsrchr(context.modulepath, _T('\\'))[1] = _T('\0');

    // Setup configuration path
    wcscpy_s(context.configfile, lengthof(context.configfile), context.modulepath);
    wcscat_s(context.configfile, lengthof(context.configfile), _T("libdatamgr.xml"));

    // Setup locale.
	GetUserDefaultLocaleName(context.localeName, lengthof(context.localeName));

    // Setup Proxy Type
    // TODO: Load this configuration from file.
    // Default to False, means that, use Json proxy to access server.
    // otherwise, use the internal http impl.
    context.enableHttp = Utility::CheckHttpEnable(context.configfile);

    context.enableHttpTransfer = Utility::CheckHttpTransferEnable(context.configfile);

    // Setup Http Timeout in milliseconds.
    context.HttpTimeoutMs = Utility::GetHttpTimeoutMs(context.configfile);

    if (context.enableHttp){
        context.proto = new HttpImpl();
    }else{
        context.proto = new JsonImpl();
    }

	// Setup local cache directory
	if (GetTempPathW(lengthof(context.cachedir), context.cachedir) <= 0){
		hr = AtlHresultFromLastError();
		goto bail;
	}
	wcscat_s(context.cachedir, lengthof(context.cachedir), _T("\\vdrivecache\\"));
	if (!PathFileExists(context.cachedir)){
		SHCreateDirectory(GetActiveWindow(), context.cachedir);
	}

	// Setup Service address
    Utility::GetServiceBase(context.configfile, context.service, lengthof(context.service));

	// Setup Username & passworld
	Utility::GetServiceUser(context.configfile, context.username, lengthof(context.username));
	Utility::GetServicePass(context.configfile, context.password, lengthof(context.password));

    // Setup JsonPort & ViewPort
    // They are both local server to delegate vdrive to get info from remote server.
    context.JsonPort = Utility::GetJsonPort(context.configfile);
    context.ViewPort = Utility::GetViewPort(context.configfile);

    // Setup api feature
    context.fastCheck = Utility::FastCheckIsEnable(context.configfile);

    // Setup PageSize of View
    Utility::GetShellViewPageSize(context.configfile, &context.pageSize);

    Utility::GetRootChildMask(context.configfile, &context.dwRootMask);

    // Check if hide search bar
    context.fEnableSearchBar = Utility::SearchBarIsEnable(context.configfile);

    // Setup transfer encoding
    context.dwTransEncoding = Utility::GetTransEncoding(context.configfile);
    OUTPUTLOG("Current Transfer Encoding is %u", context.dwTransEncoding);

	// Cleanup AccessToken
	context.AccessToken [0] = _T('\0');

	hr = S_OK;
bail:
	return hr;
}

static HRESULT DMCleanup()
{
	if (gspEdoc2Context){
        delete gspEdoc2Context->proto; gspEdoc2Context->proto = NULL;
		delete gspEdoc2Context; gspEdoc2Context = NULL;
	}
	return S_OK;
}

BOOL WINAPI DllMain(_In_  HINSTANCE hinstDLL,
                    _In_  DWORD fdwReason,
                    _In_  LPVOID lpvReserved
                    )
{
    if (DLL_PROCESS_ATTACH){
        DMInit(hinstDLL);
    }
    if (DLL_PROCESS_DETACH){
        DMCleanup();
    }
    return TRUE;
}

//////////////////////////////////////////////////////////////////////////
BOOL DMHttpIsEnable()
{
    return gspEdoc2Context && gspEdoc2Context->enableHttp;
}

BOOL DMHttpTransferIsEnable()
{
    return gspEdoc2Context && gspEdoc2Context->enableHttpTransfer;
}

BOOL DMFastCheckIsEnable()
{
    return gspEdoc2Context && gspEdoc2Context->fastCheck;
}

BOOL DMHasRootChild(DWORD dwCat){
    if (!gspEdoc2Context) return FALSE;
    return dwCat & gspEdoc2Context->dwRootMask;
}

DWORD DMGetTransEncoding()
{
    return gspEdoc2Context && gspEdoc2Context->dwTransEncoding;
}

BOOL DMSearchBarIsEnable()
{
    return gspEdoc2Context && gspEdoc2Context->fEnableSearchBar;
}

/**
 * Opens an existing .tar archive.
 * Here we basically memorize the filename of the .tar achive, and we only
 * open the file later on when it is actually needed the first time.
 */
HRESULT DMOpen(LPCWSTR pwstrFilename, TAR_ARCHIVE** ppArchive)
{
   HRESULT hr = E_FAIL;
   TAR_ARCHIVE* pArchive = new TAR_ARCHIVE();
   if( pArchive == NULL ) return E_OUTOFMEMORY;
   // Retrieve the session context ptr.
   pArchive->context = gspEdoc2Context;
   *ppArchive = pArchive;
   return S_OK;
}

/**
 * Close the archive.
 */
HRESULT DMClose(TAR_ARCHIVE* pArchive)
{
   if( pArchive == NULL ) return E_INVALIDARG;
   delete pArchive;
   return S_OK;
}

/**
 * Return the list of children of a sub-folder.
 */
HRESULT DMGetRootChildren(TAR_ARCHIVE* pArchive, RemoteId dwId, VFS_FIND_DATA ** retList, int * nListCount)
{
   CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);
   OUTPUTLOG("%s(), RemoteId={%d, %d}", __FUNCTION__, dwId.category, dwId.id);
   *retList = NULL; *nListCount = 0;
   std::list<VFS_FIND_DATA> tmpList;

   if (dwId.category == VdriveCat && dwId.id == VdriveId){
       if (DMHasRootChild(PublicCat)){
           std::list<VFS_FIND_DATA> publicList;
           GetProto(pArchive)->GetTopPublic(pArchive, publicList);
           tmpList.merge(publicList, Utility::RfsComparation);
       }

       if (DMHasRootChild(PersonCat)){
           std::list<VFS_FIND_DATA> personalList;
           GetProto(pArchive)->GetTopPersonal(pArchive, personalList);
           tmpList.merge(personalList, Utility::RfsComparation);
       }

       if (DMHasRootChild(RecycleCat)){
           VFS_FIND_DATA recycleBin;
           Utility::ConstructRecycleFolder(pArchive, recycleBin);
           tmpList.push_back(recycleBin);
       }

       if (DMHasRootChild(SearchCat)){
           VFS_FIND_DATA searchBin;
           Utility::ConstructSearchFolder(pArchive, searchBin);
           tmpList.push_back(searchBin);
       }
   }else{
	   OUTPUTLOG("Invalid RemoteId{%d,%d}", dwId.category, dwId.id);
	   return S_FALSE;
   }

   if (tmpList.size() == 0) return S_OK;

   if (S_OK != DMMalloc((LPBYTE *)retList, tmpList.size() * sizeof(VFS_FIND_DATA))){
	   return E_OUTOFMEMORY;
   }
   VFS_FIND_DATA *aList = (VFS_FIND_DATA *)(*retList);

   int index = 0;
   for(std::list<VFS_FIND_DATA>::iterator it = tmpList.begin(); 
	   it != tmpList.end(); it ++){ aList [index] = *it;
		// refine the attributes.
		VFS_FIND_DATA * pData = &aList[index];
		pData->dwFileAttributes |= FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;   
		pData->dwFileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
		if (!IsBitSet(pData->dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY))
			pData->dwFileAttributes |= FILE_ATTRIBUTE_VIRTUAL;
		index ++;
   }
   *nListCount = tmpList.size();
   return S_OK;
}

HRESULT DMSetupQuery(TAR_ARCHIVE * pArchive, const wchar_t * query, VFS_FIND_DATA * pWfd)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() Query=[%s]", __FUNCTION__, (const char *)CW2AEX<>(query, CP_UTF8));

    if (GetDB(pArchive)->find(SearchId) == GetDB(pArchive)->end())
    {
        OUTPUTLOG("%s() SearchBin not initialized.", __FUNCTION__);

        lock.Unlock();
        if (Utility::ConstructSearchFolder(pArchive, *pWfd)){
            DMAddItemToDB(pArchive, pWfd->dwId, pWfd->cFileName, IsBitSet(pWfd->dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY));
            OUTPUTLOG("%s() SearchBin constructed and add into DB now.", __FUNCTION__);
        }
        lock.Lock();
    }

    ServerItemInfo & refItem = gspGlobalDB->find(SearchId)->second;

    wcscpy_s(refItem.szQuery, lengthof(refItem.szQuery), query);

    memset(pWfd, 0, sizeof(*pWfd));
    pWfd->dwId.category = SearchCat; pWfd->dwId.id = SearchId;
    pWfd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    wcscpy_s(pWfd->cFileName, lengthof(pWfd->cFileName), refItem.szName);

    return S_OK;
}

HRESULT DMGetDocInfo(TAR_ARCHIVE* pArchive, HWND hWndOwner, RemoteId dwId, int PageSize, int PageNo, const wchar_t * sortKey, int iSortDirection, int * totalPage, ViewSettings * pVS, VFS_FIND_DATA ** retList, int * nListCount)
{
    OUTPUTLOG("%s(), RemoteId={%d, %d}, PageSize=%u, PageNo=%u, sortKey=%s, sortDirection=%d", __FUNCTION__, dwId.category, dwId.id, PageSize, PageNo, (const char *)CW2A(sortKey), iSortDirection);
    *retList = NULL; *nListCount = 0; sortKey = sortKey ? sortKey : L"";
    // not implemented for root, use default NON-PAGED version.
    if (dwId.category == VdriveCat){
        HR(DMGetRootChildren(pArchive, dwId, retList, nListCount));
        *totalPage = 1;
        return S_OK;
    }
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock); 

    std::list<std::wstring> columns;
    std::list<VFS_FIND_DATA> tmpList;
    if (dwId.category == PublicCat || dwId.category == PersonCat)
    {
        if(!GetProto(pArchive)->GetDocInfo(pArchive, hWndOwner, dwId, columns, tmpList, PageSize, PageNo, sortKey, iSortDirection, totalPage))
            return S_FALSE;
    }
    if (dwId.category == RecycleCat)
    {
        if(!GetProto(pArchive)->GetPagedRecycleItems(pArchive, hWndOwner, tmpList, PageSize, PageNo, sortKey, iSortDirection, totalPage))
            return S_FALSE;        
    }
    if (dwId.category == SearchCat){
        std::wstring query = _T("");
        if (GetDB(pArchive)->end() == GetDB(pArchive)->find(SearchId)){
            return S_FALSE;
        }
        const ServerItemInfo & refItem = GetDB(pArchive)->find(SearchId)->second;
        query = refItem.szQuery;

        if(!GetProto(pArchive)->GetPagedSearchResults(pArchive, hWndOwner, query, tmpList, PageSize, PageNo, sortKey, iSortDirection, totalPage))
            return S_FALSE;
    }
    if (!tmpList.size()) return S_FALSE;

    if (S_OK != DMMalloc((LPBYTE *)retList, tmpList.size() * sizeof(VFS_FIND_DATA))){
        return E_OUTOFMEMORY;
    }
    VFS_FIND_DATA *aList = (VFS_FIND_DATA *)(*retList);

    int index = 0;
    for(std::list<VFS_FIND_DATA>::iterator it = tmpList.begin(); 
        it != tmpList.end(); it ++){ aList [index] = *it;
        // refine the attributes.
        VFS_FIND_DATA * pData = &aList[index];
        pData->dwFileAttributes |= FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;   
        pData->dwFileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
        if (!IsBitSet(pData->dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY))
            pData->dwFileAttributes |= FILE_ATTRIBUTE_VIRTUAL;
        index ++;
    }
    *nListCount = tmpList.size();

    // And Also fill in the column info.
    int colIndex = 0;
    for (std::list<std::wstring>::iterator colIt = columns.begin(); colIt != columns.end(); colIt++)
    {
        if (colIndex < lengthof(pVS->aColumns)){
            wcscpy_s(pVS->aColumns[colIndex].colName, lengthof(pVS->aColumns[colIndex].colName), colIt->c_str());
            colIndex ++;
        }
    }
    pVS->colCount = colIndex;
    return S_OK;
}

HRESULT DMGetPageSize(TAR_ARCHIVE * pArchive, DWORD * pdwPageSize)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);
    *pdwPageSize = (pArchive->context->pageSize >= 0) ? pArchive->context->pageSize : 2;
    OUTPUTLOG("%s(), PageSize=[%d]", __FUNCTION__, *pdwPageSize);
    return S_OK;
}

/**
 * Rename a file or folder in the archive.
 * Notice that we do not support rename of a folder, which contains files, yet.
 */
HRESULT DMRename(TAR_ARCHIVE* pArchive, RemoteId itemId, LPCWSTR pwstrNewName, BOOL isFolder)
{
   CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

   OUTPUTLOG("%s(), RemoteId={%d,%d}, NewName=%s", __FUNCTION__, itemId.category, itemId.id, (const char *)CW2A(pwstrNewName));

   if (!GetProto(pArchive)->RenameItem(pArchive, itemId, pwstrNewName, isFolder)){
	   OUTPUTLOG("%s(), Failed to rename item on server.", __FUNCTION__);
	   return E_FAIL;
   }
   return S_OK;
}

/**
 * Delete a file or folder.
 */
HRESULT DMDelete(TAR_ARCHIVE* pArchive, RemoteId itemId, BOOL isFolder)
{
   CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

   OUTPUTLOG("%s(), RemoteId={%d,%d}", __FUNCTION__, itemId.category, itemId.id);

   if (!GetProto(pArchive)->DeleteItem(pArchive, itemId, isFolder))
   {
	   return E_FAIL;
   }

   return S_OK;
}

/**
* Delete files or folders.
*/
HRESULT DMBatchDelete(TAR_ARCHIVE* pArchive, const wchar_t * batchIds)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s(), RemoteId={%s}", __FUNCTION__, (const char *)CW2A(batchIds));

    if (!GetProto(pArchive)->BatchDelete(pArchive, batchIds))
    {
        return E_FAIL;
    }

    return S_OK;
}

/**
 * Create a new sub-folder in the archive.
 */
HRESULT DMCreateFolder(TAR_ARCHIVE* pArchive, RemoteId parentId, LPCWSTR pwstrFilename, VFS_FIND_DATA * pWfd)
{
   CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);
   
   if (NULL == pwstrFilename ) return E_INVALIDARG;

   OUTPUTLOG("%s(), ParentId={%d,%d}, pwstrFilename=[%s]", __FUNCTION__, parentId.category, parentId.id, (const char *)CW2A(pwstrFilename));

   if (wcsrchr(pwstrFilename, _T('\\'))){
	   pwstrFilename = wcsrchr(pwstrFilename, _T('\\')) + 1;
   }

   RemoteId folderId = {PublicCat, 0};
   if (GetProto(pArchive)->CreateFolder(pArchive, parentId, pwstrFilename, &folderId)){
	   pWfd->dwId = folderId;
	   OUTPUTLOG("%s(`%s\') return Id=[%d:%d]", __FUNCTION__, (const char *)CW2A(pwstrFilename), folderId.category, folderId.id);
	   return S_OK;
   }

   return E_FAIL;
}

/**
 * Create a new file in the archive.
 * This method expects a memory buffer containing the entire file contents.
 * HarryWu, 2014.2.2
 * Notice! an empty file can NOT be created here!
 */
HRESULT DMWriteFile(TAR_ARCHIVE* pArchive, RemoteId parentId, LPCWSTR pwstrFilename, const LPBYTE pbBuffer, DWORD dwFileSize, DWORD dwAttributes)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    if (NULL == pwstrFilename || !pbBuffer || !dwFileSize) return E_INVALIDARG;

    OUTPUTLOG("%s(),ParentID={%d,%d}, pwstrFilename=[%s], dwFileSize=[%d]", __FUNCTION__, parentId.category, parentId.id, (const char *)CW2A(pwstrFilename), dwFileSize);

    // HarryWu, 2014.2.15
    // TODO: Post file content to server
    // pbBuffer, dwFileSize
    std::wstring basename = wcsrchr(pwstrFilename, _T('\\')) ? (wcsrchr(pwstrFilename, _T('\\')) + 1): (pwstrFilename);
    wchar_t szTempFile [MAX_PATH] = _T("");
    if (!Utility::GenerateTempFilePath(szTempFile, lengthof(szTempFile), basename.c_str()))
        return S_FALSE;

    // Write content to temp file, 
    // and then upload this temp file.
    OUTPUTLOG("%s(), [Stream]: Generate temporary file to `%s\'", __FUNCTION__, (const char *)CW2A(szTempFile));

    FILE * fout = _wfopen(szTempFile, _T("wb"));
    int byteswritten = fwrite(pbBuffer, 1, dwFileSize, fout);
    if (byteswritten != dwFileSize){
        fclose(fout); return S_FALSE;
    }
    fclose(fout);

    OUTPUTLOG("%s(), write [%s] with %d bytes", __FUNCTION__, (const char *)CW2A(szTempFile), dwFileSize);

    // Upload Temporary file to server.
    if (!GetProto(pArchive)->UploadFile(pArchive, parentId, szTempFile, basename.c_str()))
        return S_FALSE;

    return S_OK;
}

/**
 * Reads a file from the archive.
 * The method returns the contents of the entire file in a memory buffer.
 */
HRESULT DMReadFile(TAR_ARCHIVE* pArchive, RemoteId itemId, LPCWSTR pwstrFilename, LPBYTE* ppbBuffer, DWORD* pdwFileSize)
{
	CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

	if (NULL == pwstrFilename ) return E_INVALIDARG;

	OUTPUTLOG("%s(), ItemId={%d,%d}, pwstrFilename=[%s]", __FUNCTION__, itemId.category, itemId.id, WSTR2ASTR(pwstrFilename));

	*ppbBuffer = NULL; *pdwFileSize = 0;

	// HarryWu, 2014.2.15
	// TODO: Read file contents from remote.
	// ...
    std::wstring basename = wcsrchr(pwstrFilename, _T('\\')) ? (wcsrchr(pwstrFilename, _T('\\')) + 1): (pwstrFilename);
    wchar_t szTempFile [MAX_PATH] = _T("");
    if (!Utility::GenerateTempFilePath(szTempFile, lengthof(szTempFile), basename.c_str()))
        return S_FALSE;

	// Download remote file to local temp file,
	// and then read content from this file.
	GetProto(pArchive)->DownloadFile(pArchive, itemId, szTempFile);
    OUTPUTLOG("%s(), [Stream]: Generate temporary file to `%s\'", __FUNCTION__, (const char *)CW2A(szTempFile));

	// Write content to temp file, 
	// and then upload this temp file.
	FILE * fin = _wfopen(szTempFile, _T("rb"));
	if (fin != NULL){
		fseek(fin, 0, SEEK_END);
		*pdwFileSize = ftell(fin);
        fseek(fin, 0, SEEK_SET);
	}

	OUTPUTLOG("%s(), read [%s] with %d bytes", __FUNCTION__, (const char *)CW2A(szTempFile), *pdwFileSize);
	DMMalloc((LPBYTE*)ppbBuffer, *pdwFileSize);
	if (*ppbBuffer && fin){
		int readbytes = fread(*ppbBuffer, 1, *pdwFileSize, fin);
        if (readbytes == *pdwFileSize) {
            fclose(fin); return S_OK;
        }
	}

	if (fin) fclose(fin);
	return S_FALSE;
}

HRESULT DMDownload(TAR_ARCHIVE * pArchive, LPCWSTR pwstrLocalDir, RemoteId itemId, BOOL isFolder, BOOL removeSource)
{
	CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

	OUTPUTLOG("%s(), local=`%s\' remote=[%d:%d], flag[`%s\'], [%s]"
		, __FUNCTION__
		, (const char *)CW2A(pwstrLocalDir)
		, itemId.category
		, itemId.id
		, removeSource ? "RemoveSource" : "KeepSource"
        , isFolder ? "Folder" : "File");

    if (isFolder){
        if (!GetProto(pArchive)->DownloadFolder(pArchive, itemId, pwstrLocalDir))
            return S_FALSE;
    }else{
        if (!GetProto(pArchive)->DownloadFile(pArchive, itemId, pwstrLocalDir))
            return S_FALSE;
    }

	return S_OK;
}

HRESULT DMUpload(TAR_ARCHIVE * pArchive, LPCWSTR pwstrLocalPath, RemoteId viewId, BOOL removeSource)
{
	CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

	OUTPUTLOG("%s(), local=`%s\' remote=[%d:%d], flag[`%s\']"
		, __FUNCTION__
		, (const char *)CW2A(pwstrLocalPath)
		, viewId.category
		, viewId.id
		, removeSource ? "RemoveSource" : "KeepSource");

    if (PathIsDirectory(pwstrLocalPath)){
        if (!GetProto(pArchive)->UploadFolder(pArchive, viewId, pwstrLocalPath))
            return S_FALSE;
    }else{
        if (!GetProto(pArchive)->UploadFile(pArchive, viewId, pwstrLocalPath, NULL))
            return S_FALSE;
    }

	return S_OK;
}

HRESULT DMGetCustomColumns(TAR_ARCHIVE * pArchive, RemoteId viewId, LPWSTR pwstrColumnList, int maxcch)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);
    
    OUTPUTLOG("%s() ViewId=[%d:%d]", __FUNCTION__, viewId.category, viewId.id);

    if (!GetProto(pArchive)->GetColumnInfo(pArchive, viewId, pwstrColumnList, maxcch))
        return S_FALSE;

    return S_OK;
}

HRESULT DMPreviewFile(TAR_ARCHIVE * pArchive, RemoteId itemId)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() itemId=[%d:%d]", __FUNCTION__, itemId.category, itemId.id);

    if (!GetProto(pArchive)->PreviewFile(pArchive, itemId))
        return S_FALSE;

    return S_OK;
}

HRESULT DMOnShellViewCreated(TAR_ARCHIVE * pArchive, HWND shellViewWnd, DWORD dwCat, DWORD dwId)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() shellViewWnd=[%p], Cat=%d, id=%d", __FUNCTION__, shellViewWnd, dwCat, dwId);

    if (!GetProto(pArchive)->OnShellViewCreated(pArchive, shellViewWnd, dwCat, dwId))
        return S_FALSE;

    return S_OK;
}

HRESULT DMOnShellViewRefreshed(TAR_ARCHIVE * pArchive, HWND shellViewWnd)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() shellViewWnd=[%p]", __FUNCTION__, shellViewWnd);

    if (!GetProto(pArchive)->OnShellViewRefreshed(pArchive, shellViewWnd))
        return S_FALSE;

    return S_OK;
}

HRESULT DMOnShellViewSized(TAR_ARCHIVE * pArchive, HWND shellViewWnd)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() shellViewWnd=[%p]", __FUNCTION__, shellViewWnd);

    if (!GetProto(pArchive)->OnShellViewSized(pArchive, shellViewWnd))
        return S_FALSE;

    return S_OK;
}

HRESULT DMOnShellViewClosing(TAR_ARCHIVE * pArchive, HWND shellViewWnd)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() shellViewWnd=[%p]", __FUNCTION__, shellViewWnd);

    if (!GetProto(pArchive)->OnShellViewClosing(pArchive, shellViewWnd))
        return S_FALSE;

    return S_OK;
}

HRESULT DMMalloc(LPBYTE * ppBuffer, DWORD dwBufSize)
{
	*ppBuffer = (LPBYTE)malloc(dwBufSize);
	if (*ppBuffer != NULL)
		return S_OK;
	else 
		return E_OUTOFMEMORY;
}

HRESULT DMRealloc(LPBYTE * ppBuffer, DWORD dwBufSize)
{
	*ppBuffer = (LPBYTE)realloc(*ppBuffer, dwBufSize);
	if (*ppBuffer != NULL)
		return S_OK;
	else 
		return E_OUTOFMEMORY;
}

HRESULT DMFree(LPBYTE pBuffer)
{
	free(pBuffer);
	return S_OK;
}

HRESULT DMGetCurrentPageNumber(TAR_ARCHIVE * pArchive, RemoteId id, DWORD * retPageNo)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%d:%d]", __FUNCTION__, id.category, id.id);

    if (GetDB(pArchive)->find(id.id) == GetDB(pArchive)->end())
        return E_FAIL;

    const ServerItemInfo & refItem = gspGlobalDB->find(id.id)->second;

    *retPageNo = refItem.dwCurPage;

    return S_OK;
}

HRESULT DMSetPageNumber(TAR_ARCHIVE * pArchive, RemoteId id, DWORD dwNewPageNo)
{
	CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

	OUTPUTLOG("%s() id=[%d:%d]", __FUNCTION__, id.category, id.id);

	if (GetDB(pArchive)->find(id.id) == GetDB(pArchive)->end())
		return E_FAIL;

	ServerItemInfo & refItem = gspGlobalDB->find(id.id)->second;

    if (dwNewPageNo <= refItem.dwTotalPage) refItem.dwCurPage = dwNewPageNo;

	return S_OK;
}

HRESULT DMIncCurrentPageNumber(TAR_ARCHIVE * pArchive, RemoteId id)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%d:%d]", __FUNCTION__, id.category, id.id);

    if (GetDB(pArchive)->find(id.id) == GetDB(pArchive)->end())
        return E_FAIL;

    ServerItemInfo & refItem = gspGlobalDB->find(id.id)->second;

    if (refItem.dwCurPage < refItem.dwTotalPage) refItem.dwCurPage ++;

    return S_OK;
}

HRESULT DMDecCurrentPageNumber(TAR_ARCHIVE * pArchive, RemoteId id)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%d:%d]", __FUNCTION__, id.category, id.id);

    if (GetDB(pArchive)->find(id.id) == GetDB(pArchive)->end())
        return E_FAIL;

    ServerItemInfo & refItem = gspGlobalDB->find(id.id)->second;

    if (refItem.dwCurPage > 1) refItem.dwCurPage --;

    return S_OK;
}

HRESULT DMSetTotalPageNumber(TAR_ARCHIVE * pArchive, RemoteId id, DWORD totalPage)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%d:%d]", __FUNCTION__, id.category, id.id);

    if (GetDB(pArchive)->find(id.id) == GetDB(pArchive)->end())
        return E_FAIL;

    ServerItemInfo & refItem = gspGlobalDB->find(id.id)->second;

    refItem.dwTotalPage = totalPage;

    return S_OK;
}

HRESULT DMAddItemToDB(TAR_ARCHIVE * pArchive, RemoteId id, wchar_t * pwstrName, BOOL isFolder)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%d:%d]", __FUNCTION__, id.category, id.id);

    ServerItemInfo item = {0};
    item.id = id.id;
    item.dwCurPage = 1;
    item.dwTotalPage = 0;
    item.isFolder = isFolder;
    wcscpy_s(item.szName, lengthof(item.szName), pwstrName);

    GetDB(pArchive)->insert(std::make_pair(id.id, item));

    return S_OK;
}

HRESULT DMLockFile(TAR_ARCHIVE * pArchive, RemoteId id, BOOL toLock)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%d:%d], lock=[%d]", __FUNCTION__, id.category, id.id, toLock);

    if (!GetProto(pArchive)->LockFile(pArchive, id, toLock))
        return S_FALSE;

    return S_OK;
}

HRESULT DMIsFileLocked(TAR_ARCHIVE * pArchive, RemoteId id, BOOL * isLocked)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%d:%d]", __FUNCTION__, id.category, id.id);
    
    return S_OK;
}

HRESULT DMInternalLink(TAR_ARCHIVE * pArchive, LPCWSTR idlist)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%s]", __FUNCTION__, (const char *)CW2A(idlist));

    if (!GetProto(pArchive)->InternalLink(pArchive, idlist))
        return S_FALSE;

    return S_OK;
}

HRESULT DMShareFile(TAR_ARCHIVE * pArchive, LPCWSTR idlist)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%s]", __FUNCTION__, (const char *)CW2A(idlist));

    if (!GetProto(pArchive)->ShareFile(pArchive, idlist))
        return S_FALSE;

    return S_OK;
}

HRESULT DMExtEditFile(TAR_ARCHIVE * pArchive, RemoteId id)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%d:%d]", __FUNCTION__, id.category, id.id);

    if (!GetProto(pArchive)->ExtEditFile(pArchive, id))
        return S_FALSE;

    return S_OK;
}

HRESULT DMDistributeFile(TAR_ARCHIVE * pArchive, LPCWSTR idlist)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%s]", __FUNCTION__, (const char *)CW2A(idlist));

    if (!GetProto(pArchive)->DistributeFile(pArchive, idlist))
        return S_FALSE;

    return S_OK;
}

HRESULT DMViewLog(TAR_ARCHIVE * pArchive, LPCWSTR idlist)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%s]", __FUNCTION__, (const char *)CW2A(idlist));

    if (!GetProto(pArchive)->ViewLog(pArchive, idlist))
        return S_FALSE;

    return S_OK;
}

HRESULT DMHistoryVersion(TAR_ARCHIVE * pArchive, RemoteId id)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%d:%d]", __FUNCTION__, id.category, id.id);

    if (!GetProto(pArchive)->HistoryVersion(pArchive, id))
        return S_FALSE;

    return S_OK;
}

HRESULT DMSelectItems(TAR_ARCHIVE * pArchive, HWND hShellViewWindow, LPCWSTR itemIds)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() ids=[`%s']", __FUNCTION__, (const char *)CW2A(itemIds ? itemIds : _T("")));

    if (!GetProto(pArchive)->SelectItems(pArchive, hShellViewWindow, itemIds ? itemIds : _T("")))
        return S_FALSE;

    return S_OK;
}

HRESULT DMFindChild(TAR_ARCHIVE * pArchive, RemoteId parentId, LPCWSTR childName, VFS_FIND_DATA * pInfo)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() parentId=[`%d:%d'], childName=`%s'", __FUNCTION__, parentId.category, parentId.id, (const char *)CW2A(childName ? childName : _T("")));

    if (parentId.category != PublicCat && parentId.category != PersonCat){
        lock.Unlock();
        int totalPage = 0; VFS_FIND_DATA * pChild = NULL; int nChildCount = 0; ViewSettings vs;
        HR (DMGetDocInfo(pArchive, NULL/*--query data only--*/, parentId, MaxPageSize, 1, NULL, 0, &totalPage, &vs, &pChild, &nChildCount));
        for (int i = 0; i < nChildCount; i ++){
            if (!wcscmp(pChild[i].cFileName, childName)){
                *pInfo = pChild[i];break;
            }
        }
        DMFree((LPBYTE)pChild);
        return S_OK;
    }

    if (!GetProto(pArchive)->FindChild(pArchive, parentId, childName, *pInfo))
        return E_FAIL;

    return S_OK;
}

HRESULT DMCheckMenu(TAR_ARCHIVE * pArchive, HWND hDefShellView, const wchar_t * idlist, MenuType * menudef)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%s], inputbits=[%llx]", __FUNCTION__, (const char *)CW2AEX<>(idlist, CP_UTF8), (*menudef));

    std::wstring sIdList = idlist;
    if (!GetProto(pArchive)->CheckMenu(pArchive, hDefShellView, sIdList, menudef))
    {
        return S_FALSE;
    }

    OUTPUTLOG("%s() id=[%s], outputbits=[%llx]", __FUNCTION__, (const char *)CW2AEX<>(idlist, CP_UTF8), (*menudef));

    return S_OK;
}

HRESULT DMRecover(TAR_ARCHIVE * pArchive, const wchar_t * idlist)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s() id=[%s]", __FUNCTION__, (const char *)CW2AEX<>(idlist, CP_UTF8));

    if (!GetProto(pArchive)->Recover(pArchive, idlist)){
        return S_FALSE;
    }

    return S_OK;
}

HRESULT DMClearRecycleBin(TAR_ARCHIVE * pArchive)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s()", __FUNCTION__);

    if (!GetProto(pArchive)->ClearRecycleBin(pArchive)){
        return S_FALSE;
    }

    return S_OK;
}

HRESULT DMMove(TAR_ARCHIVE * pArchive, const wchar_t * srcIdList, RemoteId destFolderId, BOOL fRemoveSource)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    OUTPUTLOG("%s([%s]->[%d.%d], %s)", __FUNCTION__, (const char *)CW2AEX<>(srcIdList, CP_UTF8), destFolderId.category, destFolderId.id, fRemoveSource ? "Move" : "Copy" );

    if (!GetProto(pArchive)->Move(pArchive, srcIdList, destFolderId, fRemoveSource)){
        return S_FALSE;
    }

    return S_OK;
}

HRESULT DMSetWndInfo(TAR_ARCHIVE * pArchive, HWND hWndOwner, const struct WndInfo * winfo)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    if (winfo->dwMagic != WndInfoMagic) return E_INVALIDARG;

    if (GetWndDB(pArchive)->find(hWndOwner) == GetWndDB(pArchive)->end()){
        GetWndDB(pArchive)->insert(std::make_pair(hWndOwner, *winfo));
        return S_OK;
    }

    WndInfo & wi = GetWndDB(pArchive)->find(hWndOwner)->second;

    if (wi.dwMagic == WndInfoMagic) wi = *winfo;

    return S_OK;
}

HRESULT DMGetWndInfo(TAR_ARCHIVE * pArchive, HWND hWndOwner, struct WndInfo * winfo)
{
    CComCritSecLock<CComCriticalSection> lock(pArchive->csLock);

    memset(winfo, 0, sizeof(*winfo));

    if (GetWndDB(pArchive)->find(hWndOwner) != GetWndDB(pArchive)->end())
        *winfo = GetWndDB(pArchive)->find(hWndOwner)->second;

    return S_OK;
}

