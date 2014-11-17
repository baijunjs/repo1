
#include "stdafx.h"

#include "TarFileSystem.h"
#include "ShellFolder.h"
#include "LaunchFile.h"
#include "EnumIDList.h"
#include "DataObject.h"


///////////////////////////////////////////////////////////////////////////////
// CTarFileItem

CTarFileItem::CTarFileItem(CShellFolder* pFolder, PCIDLIST_RELATIVE pidlFolder, PCITEMID_CHILD pidlItem, BOOL bReleaseItem) :
   CNseFileItem(pFolder, pidlFolder, pidlItem, bReleaseItem)
{
}

/**
 * Return SFGAOF flags for this item.
 * These flags tell the Shell about the capabilities of this item. Here we
 * can toggle the ability to copy/delete/rename an item.
 */
SFGAOF CTarFileItem::GetSFGAOF(SFGAOF dwMask)
{
   return CNseFileItem::GetSFGAOF(dwMask)
          | SFGAO_CANCOPY
          | SFGAO_CANMOVE
          | SFGAO_CANDELETE
          | SFGAO_CANRENAME
          /*| SFGAO_HASPROPSHEET*/;
}

/**
 * Return item information.
 */
HRESULT CTarFileItem::GetProperty(REFPROPERTYKEY pkey, CComPropVariant& v)
{
   if( pkey == PKEY_Volume_FileSystem ) {
      return ::InitPropVariantFromString(L"TarFS", &v);
   }
   return CNseFileItem::GetProperty(pkey, v);
}

/**
 * Set item information.
 */
HRESULT CTarFileItem::SetProperty(REFPROPERTYKEY pkey, const CComPropVariant& v)
{
   // In the Property Page, the user is allowed to
   // modify the file-attributes.
   if( pkey == PKEY_FileAttributes ) {
      return S_OK;
   }
   return CNseFileItem::SetProperty(pkey, v);
}

/**
 * Create an NSE Item instance from a child PIDL.
 */
CNseItem* CTarFileItem::GenerateChild(CShellFolder* pFolder, PCIDLIST_RELATIVE pidlFolder, PCITEMID_CHILD pidlItem, BOOL bReleaseItem)
{
   // Validate that the child PIDL really is ours
   if( !CPidlMemPtr<NSEFILEPIDLDATA>(pidlItem).IsType(TARFILE_MAGIC_ID) ) return NULL;
   // Spawn new NSE Item instance
   return new CTarFileItem(pFolder, pidlFolder, pidlItem, bReleaseItem);
}

/**
 * Create an NSE Item instance from static data.
 *
 * HarryWu, 2014.2.2
 * pass parameter <WIN32_FIND_DATA:wfd> by ref should be more effective.
 * and values in <WIN32_FIND_DATA:wfd> present in debug mode is not correct,
 * if pass <WIN32_FIND_DATA:wfd> by value.
 */
CNseItem* CTarFileItem::GenerateChild(CShellFolder* pFolder, PCIDLIST_RELATIVE pidlFolder, const VFS_FIND_DATA & wfd)
{
   NSEFILEPIDLDATA data = { sizeof(NSEFILEPIDLDATA), TARFILE_MAGIC_ID, 1, wfd };
   return new CTarFileItem(pFolder, pidlFolder, GenerateITEMID(&data, sizeof(data)), TRUE);
}

/**
 * Look up a single item by filename.
 * Validate its existance outside any cache.
 */
HRESULT CTarFileItem::GetChild(LPCWSTR pwstrName, SHGNO ParseType, CNseItem** pItem)
{
   WCHAR wszFilename[MAX_PATH] = { 0 };
   HR( _GetPathnameQuick(m_pidlFolder, m_pidlItem, wszFilename) );
   ::PathAppend(wszFilename, pwstrName);
   
   RemoteId parentId;
   HR( _GetIdQuick(m_pidlItem, &parentId));

   // HarryWu, 2014.6.5
   // there are two different policy, directly call FileExists() to query, 
   // or use Enum() api to get full list and then check it's presence.
   VFS_FIND_DATA wfd = { 0 };
   if (FAILED(DMFindChild(_GetTarArchivePtr(), parentId, pwstrName, &wfd)))
       return AtlHresultFromWin32(ERROR_FILE_NOT_FOUND);
   else{
	   *pItem = GenerateChild(m_pFolder, m_pidlFolder, wfd);
	   return S_OK;
   }
   return AtlHresultFromWin32(ERROR_FILE_NOT_FOUND);
}

/**
 * Retrieve the list of children of the current folder item.
 */
HRESULT CTarFileItem::EnumChildren(HWND hwndOwner, SHCONTF grfFlags, CSimpleValArray<CNseItem*>& aItems)
{
   // Only directories have sub-items
   if( !IsFolder() ) return E_HANDLE;

   // Get actual path and retrieve a list of sub-items
   WCHAR wszPath[MAX_PATH] = { 0 };
   HR( _GetPathnameQuick(m_pidlFolder, m_pidlItem, wszPath) );

   // HarryWu, 2014.2.18
   // Get the actual id to retrieve a list of sub-items
   RemoteId dwId = {0,0};
   HR( _GetIdQuick(m_pidlItem, &dwId));

   DWORD dwPageSize = 0;
   HR (DMGetPageSize(_GetTarArchivePtr(), &dwPageSize));

   std::wstring sSortKey = _T("");
   int iSortDirection = 0;
   struct WndInfo winfo; memset(&winfo, 0, sizeof(winfo));
   HR (DMGetWndInfo(_GetTarArchivePtr(), hwndOwner, &winfo));
   if (winfo.dwMagic == WndInfoMagic) {
        sSortKey = winfo.szSortKey;
        iSortDirection = winfo.iSortDirection;
   }

   // HarryWu, 2014.1.29
   // Note!, it is NOT safe to pass c++ objects array between modules.
   // use /MD to generate these modules.
   VFS_FIND_DATA * aList = NULL; int nListCount = 0;

   DWORD dwCurrPage = 1;
   DWORD dwTotalPage = 0;
   ViewSettings vs; memset(&vs, 0, sizeof(vs));

   if (dwPageSize){
       if (dwId.category != VdriveCat){
           /*HR*/ (DMGetCurrentPageNumber(_GetTarArchivePtr(), dwId, &dwCurrPage));
       }
       HR( DMGetDocInfo(_GetTarArchivePtr(), hwndOwner, *(RemoteId *)&dwId, dwPageSize, dwCurrPage, sSortKey.c_str(), iSortDirection, (int *)&dwTotalPage, &vs, &aList, &nListCount));
       if (dwId.id != VdriveId){
           /*HR*/( DMSetTotalPageNumber(_GetTarArchivePtr(), dwId, dwTotalPage));
       }  
   }else{
       HR( DMGetDocInfo(_GetTarArchivePtr(), hwndOwner, *(RemoteId *)&dwId, MaxPageSize, 1, sSortKey.c_str(), iSortDirection, (int *)&dwTotalPage, &vs, &aList, &nListCount));
   } 

   for( int i = 0; i < nListCount; i++ ) {
      // Filter item according to the 'grfFlags' argument
      if( SHFilterEnumItem(grfFlags, *(WIN32_FIND_DATA *)(&aList[i])) != S_OK ) continue;
      // Create an NSE Item from the file-info data
      aItems.Add( GenerateChild(m_pFolder, m_pFolder->m_pidlFolder, aList[i]) );
      // Cache it to db
      DMAddItemToDB(_GetTarArchivePtr(), aList[i].dwId, aList[i].cFileName, IsBitSet(aList[i].dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY));
   }
   
   DMFree((LPBYTE)aList);

   return S_OK;
}

/**
 * Produce a file-stream instance.
 */
HRESULT CTarFileItem::GetStream(const VFS_STREAM_REASON& Reason, CNseFileStream** ppFile)
{
   WCHAR wszFilename[MAX_PATH] = { 0 };
   HR( _GetPathnameQuick(m_pidlFolder, m_pidlItem, wszFilename) );
   
   RemoteId parentId;
   HR( _GetViewIdQuick(m_pidlFolder, &parentId));
   
   RemoteId itemId;
   HR(_GetIdQuick(m_pidlItem, &itemId));

   *ppFile = new CTarFileStream(static_cast<CTarFileSystem*>(m_pFolder->m_spFS.m_p), parentId, itemId, wszFilename, Reason.uAccess);
   return *ppFile != NULL ? S_OK : E_OUTOFMEMORY;
}

/**
 * Create a new directory.
 */
HRESULT CTarFileItem::CreateFolder()
{
   RemoteId parentId = {0,0};
   HR( _GetViewIdQuick(m_pidlFolder, &parentId));

   // Create new directory in archive
   WCHAR wszFilename[MAX_PATH] = { 0 };
   HR( _GetPathnameQuick(m_pidlFolder, m_pidlItem, wszFilename) );

   OUTPUTLOG("%s(), pidlFodler=%p, pidlItem=%p", __FUNCTION__, m_pidlFolder, m_pidlItem);

   HR( DMCreateFolder(_GetTarArchivePtr(), *(RemoteId *)&parentId, wszFilename, (VFS_FIND_DATA *)m_pWfd) );
   return S_OK;
}

/**
 * Rename this item.
 */
HRESULT CTarFileItem::Rename(LPCWSTR pstrNewName, LPWSTR pstrOutputName)
{
   WCHAR wszFilename[MAX_PATH] = { 0 };
   HR( _GetPathnameQuick(m_pidlFolder, m_pidlItem, wszFilename) );

   // The Shell often doesn't include the filename extension in the
   // renamed filename, so we'll append it now.
   if( wcschr(pstrOutputName, '.') == NULL ) ::PathAddExtension(pstrOutputName, ::PathFindExtension(wszFilename));

   // Rename the item in archive
   HR( DMRename(_GetTarArchivePtr(), *(RemoteId *)&m_pWfd->dwId, pstrOutputName, IsFolder()) );
   return S_OK;
}

/**
 * Delete this item.
 */
HRESULT CTarFileItem::Delete()
{
   HR( DMDelete(_GetTarArchivePtr(), *(RemoteId *)(&m_pWfd->dwId), IsFolder()) );

   CPidl pidl(m_pFolder->m_pidlRoot, m_pidlFolder, m_pidlItem);
   ::SHChangeNotify(SHCNE_DELETE, SHCNF_IDLIST | SHCNF_FLUSH, pidl);

   return S_OK;
}

/**
* Get Custom Columns
*/
HRESULT CTarFileItem::InitCustomColumns()
{
    wchar_t szColumns [MAX_PATH] = _T("");
    RemoteId viewId; _GetIdQuick(m_pidlItem, &viewId);
    HR( DMGetCustomColumns(_GetTarArchivePtr(), *(RemoteId *)&viewId, szColumns, lengthof(szColumns)));
    return S_OK;
}

HRESULT CTarFileItem::SelectItems(HWND hWndOwner, HWND hShellViewWindow, LPCWSTR itemIds)
{
    if (itemIds){
        OUTPUTLOG("%s(`%s')", __FUNCTION__, (const char *)CW2A(itemIds));
    }
    
    HR (DMSelectItems(_GetTarArchivePtr(), hShellViewWindow, itemIds));

    return S_OK;
}

/**
 * Returns the menu-items for an item.
 */
HMENU CTarFileItem::GetMenu()
{
   UINT uMenuRes = IDM_FILE;
   if( IsFolder() ) uMenuRes = IDM_FOLDER;
   return ::LoadMenu(_pModule->GetResourceInstance(), MAKEINTRESOURCE(uMenuRes));
}

HRESULT CTarFileItem::SelectMenuItems(HWND hDefShellView, const wchar_t* idstring, MenuType * selectedMenuItems)
{
    HR ( DMCheckMenu(_GetTarArchivePtr(), hDefShellView, idstring, selectedMenuItems));
    return S_OK;
}

/**
 * Execute a menucommand.
 */
HRESULT CTarFileItem::ExecuteMenuCommand(VFS_MENUCOMMAND& Cmd)
{
   switch( Cmd.wMenuID ) {
   case ID_FILE_PREVIEW:     return _PreviewFile(GetITEMID());
   case ID_FILE_OPEN:        return LoadAndLaunchFile(Cmd.hWnd, m_pFolder, GetITEMID());
   case ID_FILE_EXTRACT:     //return _ExtractToFolder(Cmd);
   case ID_COMMAND_EXTRACT:  return _ExtractToFolder(Cmd);
   case DFM_CMD_PASTE:       return _DoPasteFiles(Cmd);
   case DFM_CMD_NEWFOLDER:   //return _DoNewFolder(Cmd, IDS_NEWFOLDER);
   case ID_FILE_NEWFOLDER:   return _DoNewFolder(Cmd, IDS_NEWFOLDER);
   case ID_FILE_PROPERTIES:  return _DoShowProperties(Cmd);
   case ID_FILE_PREV:        return _PrevPage(Cmd);
   case ID_FILE_NEXT:        return _NextPage(Cmd);
   case ID_FILE_GOTO:        return _GotoPage(Cmd);
   case ID_FILE_SHARE:       return _Share(Cmd);
   case ID_FILE_UPLOAD:      return _Upload(Cmd);
   case ID_FILE_INNERLINK:   return _InternalLink(Cmd);
   case ID_FILE_DISTRIBUTE:  return _Distribute(Cmd);
   case ID_FILE_LOCK:        return _LockFile(Cmd, TRUE);
   case ID_FILE_UNLOCK:      return _LockFile(Cmd, FALSE);
   case ID_FILE_OLDVERSION:  return _HistoryVersion(Cmd);
   case ID_FILE_VIEWLOG:     return _ViewLog(Cmd);
   case ID_FILE_EXTEDIT:     return _ExtEdit(Cmd);
   case ID_FILE_SEARCH:      return _Search(Cmd);
   case ID_FILE_RECOVER:     return _Recover(Cmd);
   case ID_FILE_CLEAR_ALL:   return _ClearRecycleBin(Cmd);
   case DFM_CMD_DELETE:      return Delete();
   }
   return E_NOTIMPL;
}

// Implementation

TAR_ARCHIVE* CTarFileItem::_GetTarArchivePtr() const
{
   // NOTE: There is a nasty downcast going on here. It is c++ safe since we have
   //       single inheritance only.
   return static_cast<CTarFileSystem*>(m_pFolder->m_spFS.m_p)->m_pArchive;
}

/**
 * Extract a file from tar archive to file-system.
 * This method is invoked from one of the extra menuitems or command buttons
 * we added and prompts the user to extract the selected item(s) to a particular
 * disk folder.
 */
HRESULT CTarFileItem::_ExtractToFolder(VFS_MENUCOMMAND& Cmd)
{
   // Prompt the user to choose the target path first...
   if( Cmd.pUserData == NULL ) {
      CPidl pidlFolder;
      CComBSTR bstrTitle;
      bstrTitle.LoadString(IDS_EXTRACTTITLE);
      BROWSEINFO bi = { Cmd.hWnd, NULL, NULL, bstrTitle, BIF_RETURNONLYFSDIRS | BIF_USENEWUI | BIF_VALIDATE, NULL, 0, 0 };
      pidlFolder.Attach( ::SHBrowseForFolder(&bi) );
      if( pidlFolder.IsNull() ) return E_ABORT;      
      LPTSTR pstrTarget = (LPTSTR) malloc(MAX_PATH * sizeof(TCHAR));
      HR( ::SHGetPathFromIDList(pidlFolder, pstrTarget) );
      Cmd.pUserData = pstrTarget;
   }
   
   if (!DMHttpTransferIsEnable())
   {
       // HarryWu, 2014.3.14
       // Task: here is a chance to post the task to external tool.

   	   DWORD dwCount = 0;
	   if (Cmd.pShellItems) Cmd.pShellItems->GetCount(&dwCount);

	   HR( DMDownload(_GetTarArchivePtr()
		   , (LPCTSTR)Cmd.pUserData
		   , *(RemoteId *)&m_pWfd->dwId
           , IsFolder()
		   , Cmd.dwDropEffect == DROPEFFECT_MOVE));

       return S_OK;
   } 

   // Extract this item to target folder.
   // Let the Windows CopyEngine do the dirty work simply by copying the
   // file/folder out of this item.
   if( Cmd.pFO == NULL ) {
      HR( ::SHCreateFileOperation(Cmd.hWnd, FOF_NOCOPYSECURITYATTRIBS | FOFX_NOSKIPJUNCTIONS | FOF_NOCONFIRMMKDIR, &Cmd.pFO) );
   }
   if( IsRoot() ) {
	   // HarryWu, 2014.2.28
	   // Do not support of downloading entire tree.
	   return E_NOTIMPL;
   }else {
      // Item is a file/folder inside the archive
      CComPtr<IShellItem> spSourceFile, spTargetFolder;
      CPidl pidlFile(m_pFolder->m_pidlRoot, m_pidlFolder, m_pidlItem);
      HR( ::SHCreateItemFromIDList(pidlFile, IID_PPV_ARGS(&spSourceFile)) );
      HR( ::SHCreateItemFromParsingName(static_cast<LPCTSTR>(Cmd.pUserData), NULL, IID_PPV_ARGS(&spTargetFolder)) );
      HR( Cmd.pFO->CopyItem(spSourceFile, spTargetFolder, m_pWfd->cFileName, NULL) );
   }
   return S_OK;
}

/**
* Paste a data-object as a bunch of files.
* Assumes that the passed IDataObject contains files to paste into
* our virtual file-system.
*/
HRESULT CTarFileItem::_DoPasteFiles(VFS_MENUCOMMAND& Cmd)
{
    ATLASSERT(IsFolder());
    ATLASSERT(Cmd.pDataObject);
    if( Cmd.pDataObject == NULL ) return E_FAIL;

    // HarryWu, 2014.3.14
    // Task: Hook upload, EVENT_OBJECT_DRAGDROPPED
    // http://msdn.microsoft.com/en-us/library/windows/desktop/dd318066(v=vs.85).aspx
    // DragCurrent Process Not Encrypt Or not Control ClipboardCDataObject::GetData  cf=UntrustedDragDrop
    if (!DMHttpTransferIsEnable())
    {
        RemoteId DestId; _GetIdQuick(m_pidlItem, &DestId);

		FORMATETC fmtec; 
		fmtec.cfFormat = CF_HDROP;
		fmtec.dwAspect = DVASPECT_CONTENT;
		fmtec.lindex   = -1;
		fmtec.ptd      = NULL;
		fmtec.tymed    = TYMED_HGLOBAL;

		STGMEDIUM medium;
		/*HR*/( Cmd.pDataObject->GetData(&fmtec, &medium) );
		
		if (medium.hGlobal == NULL) {
			fmtec.cfFormat = CDataObject::s_cfFILEDESCRIPTOR;
			fmtec.dwAspect = DVASPECT_CONTENT;
			fmtec.lindex   = -1;
			fmtec.ptd      = NULL;
			fmtec.tymed    = TYMED_HGLOBAL;
			Cmd.pDataObject->GetData(&fmtec, &medium);
			if (medium.hGlobal == NULL) return S_FALSE;
		}

		if (fmtec.cfFormat == CF_HDROP){
			int fileCount = DragQueryFile((HDROP)medium.hGlobal, -1, NULL, 0);
			std::wstring strFileList = _T("");
			for (int i = 0; i < fileCount; i++){
				int pathlen = DragQueryFile((HDROP)medium.hGlobal, i, NULL, 0);
				if (pathlen <= 0) continue ;
				TCHAR * szFullPath = (TCHAR *)malloc(sizeof(TCHAR) * (pathlen + 1));
				szFullPath [pathlen ] = _T('\0');
				pathlen = DragQueryFile((HDROP) medium.hGlobal, i, szFullPath, pathlen + 1);
				if (pathlen <= 0) {
					free(szFullPath);continue ;
				}
				// HarryWu, 2014.3.14
				// Post task to external tool
                strFileList += szFullPath; strFileList += _T("|");
				free(szFullPath);
			}
            if (!strFileList.empty()) DMUpload(_GetTarArchivePtr(), strFileList.c_str(), DestId, IsBitSet(Cmd.dwDropEffect, DROPEFFECT_MOVE) );
		}
		if (fmtec.cfFormat == CDataObject::s_cfFILEDESCRIPTOR){
			FILEGROUPDESCRIPTOR * pFgd = (FILEGROUPDESCRIPTOR *)GlobalLock(medium.hGlobal);
            std::wstring sIdList;
			for (int i = 0; i < pFgd->cItems; i ++){
				std::wstring filepath = pFgd->fgd[i].cFileName;
                OUTPUTLOG("%s(), Action=[%s], Source=[{%d.%d}:{%d.%d}:{%s}], Dest=[%d.%d:{%s}]", __FUNCTION__
                    , IsBitSet(Cmd.dwDropEffect, DROPEFFECT_MOVE) ? "Move" : "Copy"
                    , pFgd->fgd[i].pointl.x
                    , pFgd->fgd[i].pointl.y
                    , pFgd->fgd[i].sizel.cx
                    , pFgd->fgd[i].sizel.cy
                    , (const char *)(CW2A(filepath.c_str()))
                    , m_pWfd->dwId.category
                    , m_pWfd->dwId.id
                    , (const char *)CW2A(m_pWfd->cFileName));
                
                // HarryWu, 2014.7.31
                // Cut with Same origin
                if (pFgd->fgd[i].pointl.x == m_pWfd->dwId.category 
                    && pFgd->fgd[i].pointl.y == m_pWfd->dwId.id
                    && IsBitSet(Cmd.dwDropEffect, DROPEFFECT_MOVE)){
                    OUTPUTLOG("%s(), Cut with Same Origin, ignore it.");
                }else{
                    RemoteId srcId = {pFgd->fgd[i].sizel.cx, pFgd->fgd[i].sizel.cy};
                    wchar_t szBuf[100] = _T(""); swprintf_s(szBuf, lengthof(szBuf), _T("%d.%d"), srcId.category, srcId.id);
                    sIdList += szBuf;
                    sIdList += _T(";");
                }
			}
            if (!sIdList.empty())
                DMMove(_GetTarArchivePtr(), sIdList.c_str(), DestId, IsBitSet(Cmd.dwDropEffect, DROPEFFECT_MOVE) );

			GlobalUnlock(medium.hGlobal);
		}
        return S_OK;
    }

    // Do paste operation...
    if( Cmd.pFO == NULL ) {
        HR( ::SHCreateFileOperation(Cmd.hWnd
            // HarryWu, 2014.2.4
            // I try to skip the prompt, default to 'Yes'!
            // by OR the flag FOF_NOCONFIRMATION.
            , FOF_NOCOPYSECURITYATTRIBS | FOF_NOCONFIRMMKDIR | FOFX_NOSKIPJUNCTIONS | FOF_NOCONFIRMATION
            , &Cmd.pFO) );
    }
    // FIX: The Shell complains about E_INVALIDARG on IFileOperation::MoveItems() so we'll
    //      do the file-operation in two steps.
    CComPtr<IShellItem> spTargetFolder;
    HR( ::SHCreateItemFromIDList(_GetFullPidl(), IID_PPV_ARGS(&spTargetFolder)) );
    Cmd.pFO->CopyItems(Cmd.pDataObject, spTargetFolder);
    if( Cmd.dwDropEffect == DROPEFFECT_MOVE ) {
        Cmd.pFO->DeleteItems(Cmd.pDataObject);
    }
    // We handled this operation successfully for all items in selection
    return NSE_S_ALL_DONE;
}

HRESULT CTarFileItem::_PreviewFile( PCITEMID_CHILD pidl)
{
	RemoteId itemId = m_pWfd->dwId;

    if (!DMPreviewFile(_GetTarArchivePtr(), *(RemoteId *)&itemId))
        return S_FALSE;

	return S_OK;
}

HRESULT CTarFileItem::OnShellViewCreated(HWND hWndOwner, HWND shellViewWnd, DWORD dwCat, DWORD dwId)
{
    if (!DMOnShellViewCreated(_GetTarArchivePtr(), shellViewWnd, dwCat, dwId))
        return S_FALSE;

    return S_OK;
}

HRESULT CTarFileItem::OnShellViewSized(HWND hWndOwner, HWND shellViewWnd)
{
    if (!DMOnShellViewSized(_GetTarArchivePtr(), shellViewWnd))
        return S_FALSE;

    return S_OK;
}

HRESULT CTarFileItem::OnShellViewRefreshed(HWND hWndOwner, HWND shellViewWnd)
{
    if (!DMOnShellViewRefreshed(_GetTarArchivePtr(), shellViewWnd))
        return S_FALSE;

    return S_OK;
}

HRESULT CTarFileItem::OnShellViewClosing(HWND hWndOwner, HWND shellViewWnd)
{
    if (!DMOnShellViewClosing(_GetTarArchivePtr(), shellViewWnd))
        return S_FALSE;

    return S_OK;
}

HRESULT CTarFileItem::_PrevPage( VFS_MENUCOMMAND & Cmd)
{
    HR( DMDecCurrentPageNumber(_GetTarArchivePtr(), m_pWfd->dwId));
    _RefreshFolderView();
    return S_OK;
}

HRESULT CTarFileItem::_NextPage( VFS_MENUCOMMAND & Cmd)
{
    HR( DMIncCurrentPageNumber(_GetTarArchivePtr(), m_pWfd->dwId));
    _RefreshFolderView();
    return S_OK;
}

HRESULT CTarFileItem::_GotoPage( VFS_MENUCOMMAND & Cmd)
{
	DWORD dwNewPageNo = 0;
	if (Cmd.pUserData){
		dwNewPageNo = *(DWORD *)Cmd.pUserData;
	}
	HR( DMSetPageNumber(_GetTarArchivePtr(), m_pWfd->dwId, dwNewPageNo));
	_RefreshFolderView();
	return S_OK;
}

HRESULT CTarFileItem::_Share(VFS_MENUCOMMAND & Cmd)
{
    std::wstring idlist = GetSelectedIdList(Cmd);
    HR ( DMShareFile(_GetTarArchivePtr(), idlist.c_str()));
    return S_OK;
}

HRESULT CTarFileItem::_Upload(VFS_MENUCOMMAND & Cmd)
{
	return S_OK;
}

HRESULT CTarFileItem::_InternalLink(VFS_MENUCOMMAND & Cmd)
{
    std::wstring idlist = GetSelectedIdList(Cmd);
    HR ( DMInternalLink(_GetTarArchivePtr(), idlist.c_str()));
    return S_OK;
}

HRESULT CTarFileItem::_Distribute(VFS_MENUCOMMAND & Cmd)
{
    std::wstring idlist = GetSelectedIdList(Cmd);
    HR ( DMDistributeFile(_GetTarArchivePtr(), idlist.c_str()));
    return S_OK;
}

HRESULT CTarFileItem::_LockFile(VFS_MENUCOMMAND & Cmd, BOOL lock)
{
    HR ( DMLockFile(_GetTarArchivePtr(), m_pWfd->dwId, lock));
    return S_OK;
}

HRESULT CTarFileItem::_HistoryVersion(VFS_MENUCOMMAND & Cmd)
{
    HR ( DMHistoryVersion(_GetTarArchivePtr(), m_pWfd->dwId));
    return S_OK;
}

HRESULT CTarFileItem::_ViewLog(VFS_MENUCOMMAND & Cmd)
{
    std::wstring idlist = GetSelectedIdList(Cmd);
    HR ( DMViewLog(_GetTarArchivePtr(), idlist.c_str()));
    return S_OK;
}

HRESULT CTarFileItem::_ExtEdit(VFS_MENUCOMMAND & Cmd)
{
    HR ( DMExtEditFile(_GetTarArchivePtr(), m_pWfd->dwId));
    return S_OK;
}

HRESULT CTarFileItem::_Search(VFS_MENUCOMMAND & Cmd)
{
    std::wstring query = _T("Undefined");
    if (Cmd.pUserData){
        query = (const wchar_t *)CA2WEX<>((const char *)Cmd.pUserData, CP_ACP);
    }

    VFS_FIND_DATA searchItem;
    // HarryWu, 2014.4.13
    // TODO: Prompt user to input a query, while the 2nd parameter is empty.
    // ...
    HR ( DMSetupQuery(_GetTarArchivePtr(), query.c_str(), &searchItem));
    NSEFILEPIDLDATA data = { sizeof(NSEFILEPIDLDATA), TARFILE_MAGIC_ID, 1, searchItem };
    PCITEMID_CHILD pidlChild = GenerateITEMID(&data, sizeof(data));

    SHELLEXECUTEINFO sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_IDLIST;
    sei.hwnd = ::GetActiveWindow();
    sei.lpVerb = TEXT("explore"); // <-- not "open"

    CPidl searchPidl = m_pFolder->m_pidlRoot;
    searchPidl.Append(pidlChild); CoTaskMemFree((LPVOID)pidlChild);
    sei.lpIDList = searchPidl.GetData(); // <-- your pidl

    if (S_OK != m_pFolder->_NaviageTo((PCUITEMID_CHILD)sei.lpIDList)){
        sei.nShow = SW_SHOW;
        ShellExecuteEx(&sei);
    }

    return S_OK;
}

std::wstring CTarFileItem::GetSelectedIdList(VFS_MENUCOMMAND & Cmd)
{
    std::wstring idlist;
    if (Cmd.pShellItems){
        DWORD selectedCnt = 0;
        Cmd.pShellItems->GetCount(&selectedCnt);
        for (int i = 0; i < selectedCnt; i ++)
        {
            CComPtr<IShellItem> spShellItem;
            if (S_OK != Cmd.pShellItems->GetItemAt(i, &spShellItem) )
            {
                return idlist;
            }
            CPidl pidl; pidl.CreateFromObject(spShellItem);
            NSEFILEPIDLDATA * pInfo = (NSEFILEPIDLDATA *)pidl.GetLastItem();
            if (pInfo){
                wchar_t buf [100] = _T("");
                swprintf_s(buf, 100, _T("%d:%d,"), pInfo->wfd.dwId.id, IsBitSet(pInfo->wfd.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0);
                idlist += buf;
            }
        }
    }
    return idlist;
}

HRESULT CTarFileItem::_Recover(VFS_MENUCOMMAND & Cmd)
{
    std::wstring idlist = GetSelectedIdList(Cmd);
    HR ( DMRecover(_GetTarArchivePtr(), idlist.c_str()));
    _RefreshFolderView();
    return S_OK;
}

HRESULT CTarFileItem::_ClearRecycleBin(VFS_MENUCOMMAND & Cmd)
{
    HR ( DMClearRecycleBin(_GetTarArchivePtr()));
    _RefreshFolderView();
    return S_OK;
}

HRESULT CTarFileItem::_DoNewFolder(VFS_MENUCOMMAND& Cmd, UINT uLabelRes)
{
	//HarryWu, 2014.11.17
	//Check existence first.
	CComBSTR bstrLabel;
	bstrLabel.LoadString(uLabelRes);

	CNseItemPtr spPrevItem;
	this->GetChild(bstrLabel.m_str, SHGDN_FORPARSING, &spPrevItem);
	if( spPrevItem != NULL ) {
		return AtlHresultFromWin32(ERROR_FILE_EXISTS);
	}
	return CNseBaseItem::_DoNewFolder(Cmd, uLabelRes);
}

HRESULT CTarFileItem::Resort(HWND hWndOwner, HWND hShellView, const wchar_t *sortKey, int iSortDirection)
{
    // Only directories have sub-items
    if( !IsFolder() ) return E_HANDLE;
   
    struct WndInfo wi;
    HR (DMGetWndInfo(_GetTarArchivePtr(), hWndOwner, &wi));
    if (wi.dwMagic != WndInfoMagic) {
        memset(&wi, 0, sizeof(wi)); wi.dwMagic = WndInfoMagic;
    }
    wcscpy_s(wi.szSortKey, lengthof(wi.szSortKey), sortKey ? sortKey : _T(""));
    wi.iSortDirection = iSortDirection;
    HR (DMSetWndInfo(_GetTarArchivePtr(), hWndOwner, &wi));

    _RefreshFolderView();

    return S_OK;
}