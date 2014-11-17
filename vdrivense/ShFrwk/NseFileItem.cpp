
#include "stdafx.h"

#include "NseFileSystem.h"
#include "ShellFolder.h"
#include "PropSheetExt.h"

///////////////////////////////////////////////////////////////////////////////
// NSE File Item - Default virtual folder/file implementation


CNseFileItem::CNseFileItem(CShellFolder* pFolder, PCIDLIST_RELATIVE pidlFolder, PCITEMID_CHILD pidlItem, BOOL bReleaseItem) :
   CNseBaseItem(pFolder, pidlFolder, pidlItem, bReleaseItem)
{
   if( pidlItem == NULL ) 
   {
      // Create empty root item
      static VFS_FIND_DATA s_wfdEmpty = { 0 };
      m_pWfd = &s_wfdEmpty;
      m_pWfd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
   }
   else 
   {
      // Extract item data
      m_pWfd = &(reinterpret_cast<NSEFILEPIDLDATA*>(const_cast<PUITEMID_CHILD>(pidlItem)))->wfd;
      // Our PIDL structure must be laid out with a WIN32_FIND_DATA structure first.
      // TODO: Type-safety?
      ATLASSERT(pidlItem->mkid.cb >= sizeof(NSEFILEPIDLDATA));
   }
}

/**
 * Get the item type.
 */
BYTE CNseFileItem::GetType()
{
   return m_pidlItem == NULL ? 0 : ((NSEFILEPIDLDATA*) m_pidlItem)->magic;
}

/**
 * Get system icon index.
 * Return the System Icon index if the file can be mapped to a file.
 * Return S_FALSE if no system index exists.
 */
HRESULT CNseFileItem::GetSysIcon(UINT uIconFlags, int* pIconIndex)
{
   // Use our SHGetFileSysIcon() method to get the System Icon index
    // Note, to use ILEmpty to check pidl `is' null.
    // ==> http://msdn.microsoft.com/en-us/library/windows/desktop/bb776454(v=vs.85).aspx
    if (::ILIsEmpty(m_pidlFolder) && m_pidlItem && !::ILIsEmpty(m_pidlItem) && ::ILIsEmpty(::ILNext(m_pidlItem)) ){
        static int siRootBeginIndex = 0;
        static std::map<int, std::string> imageMap;
        if (imageMap.size() == 0){
            // Initialize a table for imagelist of different size.
            imageMap.insert(std::make_pair(SHIL_LARGE,       "Images\\RootIcons_32x32.bmp"));
            imageMap.insert(std::make_pair(SHIL_SMALL,       "Images\\RootIcons_16x16.bmp"));
            imageMap.insert(std::make_pair(SHIL_SYSSMALL,    "Images\\RootIcons_SysSmall.bmp"));
            imageMap.insert(std::make_pair(SHIL_EXTRALARGE,  "Images\\RootIcons_48x48.bmp"));
            //TODO: but jumbo will fail, :(
            imageMap.insert(std::make_pair(SHIL_JUMBO,       "Images\\RootIcons_256x256.bmp"));

            // Load the imagelist and setup [RootBeginIndex_Large] with index of newly append image.
            for (std::map<int, std::string>::iterator it = imageMap.begin(); it != imageMap.end(); it ++){
                HIMAGELIST hImageList = NULL;
                SHGetImageList(it->first, IID_IImageList, (void **)&hImageList);
                if (!hImageList) continue ;
                char szFullResPath [MAX_PATH] = "";
                GetModuleFileNameA(_pModule->get_m_hInst(), szFullResPath, lengthof(szFullResPath));
                strrchr(szFullResPath, _T('\\'))[1] = 0; strcat_s(szFullResPath, lengthof(szFullResPath), it->second.c_str());
                HBITMAP hBmp = (HBITMAP )LoadImageA(_pModule->m_hInstResource, szFullResPath, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
                if (hBmp) {
                    int iRootBeginIndex = ImageList_Add(hImageList, hBmp, NULL);
                    OUTPUTLOG("Load [%s] at Index=%u, error=%d", szFullResPath, iRootBeginIndex, GetLastError());
                    if (iRootBeginIndex != -1)
                        siRootBeginIndex = iRootBeginIndex;
                    DeleteObject(hBmp); hBmp = NULL;
                }
            }
        }

        OUTPUTLOG("%s(Flags=0x%08x)", __FUNCTION__, uIconFlags);
        if (m_pWfd->dwId.category == PublicCat ) *pIconIndex = siRootBeginIndex + 0;
        if (m_pWfd->dwId.category == PersonCat ) *pIconIndex = siRootBeginIndex + 1;
        if (m_pWfd->dwId.category == SearchCat ) *pIconIndex = siRootBeginIndex + 2;
        if (m_pWfd->dwId.category == RecycleCat) *pIconIndex = siRootBeginIndex + 3;
		return S_OK;
	}
   return ::SHGetFileSysIcon(m_pWfd->cFileName, m_pWfd->dwFileAttributes, uIconFlags, pIconIndex);
}

/**
 * Create Shell object for extracting icon and image.
 * We should create the Shell object asked for by the Shell to generate
 * the display icon or thumbnail image.
 */
HRESULT CNseFileItem::GetExtractIcon(REFIID riid, LPVOID* ppRetVal)
{
   // Use the SHCreateFileExtractIcon() method to create a default object
   // for IID_IExtractIcon. We don't support IID_IExtractImage here.
   return ::SHCreateFileExtractIcon(m_pWfd->cFileName, m_pWfd->dwFileAttributes, riid, ppRetVal);
}

/**
 * Returns whether the drop is allowed.
 * Return S_FALSE to deny the drop on the folder.
 */
HRESULT CNseFileItem::IsDropDataAvailable(IDataObject* pDataObj)
{
   // We support file drops...
   return DataObj_HasFileClipFormat(pDataObj) ? S_OK : S_FALSE;
}

/**
 * Get information about column definition.
 * We return details of a column requested, plus several additional 
 * information bits that the property system can support. We decide which
 * columns to show by default here too.
 */
HRESULT CNseFileItem::GetColumnInfo(UINT iColumn, VFS_COLUMNINFO& Column)
{
   static VFS_COLUMNINFO aColumns[] = {
      { PKEY_ItemNameDisplay,             SHCOLSTATE_TYPE_STR  | SHCOLSTATE_ONBYDEFAULT,                    0 },
	  { PKEY_Size,                        SHCOLSTATE_TYPE_INT  | SHCOLSTATE_ONBYDEFAULT,                    0 },
      { PKEY_DateCreated,                 SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT,                    0 },
      { PKEY_DateModified,                SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT,                    0 },
      { PKEY_ItemTypeText,                SHCOLSTATE_TYPE_STR  | SHCOLSTATE_ONBYDEFAULT,                    0 },
	  { PKEY_FileVersion,                 SHCOLSTATE_TYPE_STR  | SHCOLSTATE_ONBYDEFAULT,                    0 },
	  { PKEY_Author,                      SHCOLSTATE_TYPE_STR  | SHCOLSTATE_ONBYDEFAULT,                    0 },
	  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
      { PKEY_PerceivedType,               SHCOLSTATE_TYPE_INT  | SHCOLSTATE_HIDDEN,                         VFS_COLF_NOTCOLUMN },
      { PKEY_SFGAOFlags,                  SHCOLSTATE_TYPE_INT  | SHCOLSTATE_HIDDEN,                         VFS_COLF_NOTCOLUMN },
      { PKEY_Volume_IsRoot,               SHCOLSTATE_TYPE_INT  | SHCOLSTATE_HIDDEN,                         VFS_COLF_NOTCOLUMN },
      { PKEY_PropList_InfoTip,            SHCOLSTATE_TYPE_STR  | SHCOLSTATE_HIDDEN,                         VFS_COLF_NOTCOLUMN },
      { PKEY_PropList_TileInfo,           SHCOLSTATE_TYPE_STR  | SHCOLSTATE_HIDDEN,                         VFS_COLF_NOTCOLUMN },
      { PKEY_PropList_FullDetails,        SHCOLSTATE_TYPE_STR  | SHCOLSTATE_HIDDEN,                         VFS_COLF_NOTCOLUMN },
      { PKEY_PropList_PreviewTitle,       SHCOLSTATE_TYPE_STR  | SHCOLSTATE_HIDDEN,                         VFS_COLF_NOTCOLUMN },
      { PKEY_PropList_PreviewDetails,     SHCOLSTATE_TYPE_STR  | SHCOLSTATE_HIDDEN,                         VFS_COLF_NOTCOLUMN },
      { PKEY_PropList_ExtendedTileInfo,   SHCOLSTATE_TYPE_STR  | SHCOLSTATE_HIDDEN,                         VFS_COLF_NOTCOLUMN },
   };
   // HarryWu, 2014.2.28
   // to customize the columns set,
   // implement a GetColumnSet() interface in CTarFileItem,
   // and return an array as above, 
   if( iColumn >= lengthof(aColumns) ) {
	   return E_FAIL;
   }
   Column = aColumns[iColumn];
   return S_OK;
}

/**
* Get information about column definition.
* We return details of a column requested, plus several additional 
* information bits that the property system can support. We decide which
* columns to show by default here too.
*/
HRESULT CNseFileItem::GetExtColumnInfo(UINT iColumn, VFS_COLUMNINFO& Column)
{
	return S_OK;
}

/**
 * Return a property page for the item.
 */
HRESULT CNseFileItem::GetPropertyPage(CNsePropertyPage** ppPage)
{
   if( IsRoot() ) *ppPage = new CNseFileRootPropertyPage();
   else *ppPage = new CNseFileItemPropertyPage();
   return S_OK;
}

/**
 * Return item information.
 * We support the properties for the columns as well as a number of
 * administrative information bits (such as what properties to display
 * in the Details panel).
 */
HRESULT CNseFileItem::GetProperty(REFPROPERTYKEY pkey, CComPropVariant& v)
{
   ATLASSERT(v.vt==VT_EMPTY);
   if( pkey == PKEY_ParsingName ) {
      return ::InitPropVariantFromString(m_pWfd->cFileName, &v);
   } 
   if( pkey == PKEY_ItemName ) {
      return ::InitPropVariantFromString(m_pWfd->cFileName, &v);
   }
   if( pkey == PKEY_ItemNameDisplay ) {
      SHFILEINFO sfi = { 0 }; 
      ::SHGetFileInfo(m_pWfd->cFileName, m_pWfd->dwFileAttributes, &sfi, sizeof(sfi), SHGFI_USEFILEATTRIBUTES | SHGFI_DISPLAYNAME);
      return ::InitPropVariantFromString(sfi.szDisplayName, &v);
   }
   if( pkey == PKEY_FileAttributes ) {
      return ::InitPropVariantFromUInt32(m_pWfd->dwFileAttributes, &v);
   }
   if( pkey == PKEY_ItemType ) {
      return ::InitPropVariantFromString(::PathFindExtension(m_pWfd->cFileName), &v);
   }
   if( pkey == PKEY_ItemTypeText ) {
      SHFILEINFO sfi = { 0 }; 
      ::SHGetFileInfo(m_pWfd->cFileName, m_pWfd->dwFileAttributes, &sfi, sizeof(sfi), SHGFI_USEFILEATTRIBUTES | SHGFI_TYPENAME);
      return ::InitPropVariantFromString(sfi.szTypeName, &v);
   }
   if( pkey == PKEY_FileName ) {
      return ::InitPropVariantFromString(m_pWfd->cFileName, &v);
   }
   if( pkey == PKEY_FileExtension ) {
      return ::InitPropVariantFromString(::PathFindExtension(m_pWfd->cFileName), &v);
   }
   if( pkey == PKEY_DateCreated ) {
      return ::InitPropVariantFromFileTime(&m_pWfd->ftCreationTime, &v);
   }
   if( pkey == PKEY_DateModified ) {
      return ::InitPropVariantFromFileTime(&m_pWfd->ftLastWriteTime, &v);
   }
   if( pkey == PKEY_DateAccessed ) {
      return ::InitPropVariantFromFileTime(&m_pWfd->ftLastAccessTime, &v);
   }
   if( pkey == PKEY_Author ){
       return ::InitPropVariantFromString(m_pWfd->creatorName, &v);
   }
   if( pkey == PKEY_FileVersion ){
       return ::InitPropVariantFromString(m_pWfd->versionStr, &v);
   }
   if( IsFolder() ) 
   {
      if( pkey == PKEY_PerceivedType ) {
         return ::InitPropVariantFromInt32(PERCEIVED_TYPE_FOLDER, &v);
      }
      if( pkey == PKEY_Volume_IsRoot ) {
         return ::InitPropVariantFromBoolean(IsRoot(), &v);
      }
      if( pkey == PKEY_PropList_InfoTip ) {
         return ::InitPropVariantFromString(L"prop:System.ItemTypeText;", &v);
      }
      if( pkey == PKEY_PropList_TileInfo ) {
         return ::InitPropVariantFromString(L"prop:System.ItemTypeText;", &v);
      }
      if( pkey == PKEY_PropList_ExtendedTileInfo ) {
         return ::InitPropVariantFromString(L"prop:System.ItemTypeText;", &v);
      }
      if( pkey == PKEY_PropList_PreviewTitle ) {
         return ::InitPropVariantFromString(L"prop:System.ItemNameDisplay;System.ItemTypeText;", &v);
      }
      if( pkey == PKEY_PropList_PreviewDetails ) {
         return ::InitPropVariantFromString(L"prop:System.DateModified;", &v);
      }
      if( pkey == PKEY_PropList_FullDetails ) {
         return ::InitPropVariantFromString(L"prop:System.ItemNameDisplay;System.ItemTypeText;", &v);
      }
   }
   else
   {
      if( pkey == PKEY_PerceivedType ) {
         return ::InitPropVariantFromInt32(PERCEIVED_TYPE_UNKNOWN, &v);
      }
      if( pkey == PKEY_Size ) {
         return ::InitPropVariantFromUInt64(m_pWfd->nFileSizeLow, &v);
      }
      if( pkey == PKEY_PropList_InfoTip ) {
         return ::InitPropVariantFromString(L"prop:System.ItemTypeText;System.Size;", &v);
      }
      if( pkey == PKEY_PropList_TileInfo ) {
         return ::InitPropVariantFromString(L"prop:System.ItemTypeText;System.Size;", &v);
      }
      if( pkey == PKEY_PropList_ExtendedTileInfo ) {
         return ::InitPropVariantFromString(L"prop:System.ItemTypeText;System.Size;", &v);
      }
      if( pkey == PKEY_PropList_PreviewTitle ) {
         return ::InitPropVariantFromString(L"prop:System.ItemNameDisplay;System.ItemTypeText;", &v);
      }
      if( pkey == PKEY_PropList_PreviewDetails ) {
         return ::InitPropVariantFromString(L"prop:System.DateCreated;System.Size;", &v);
      }
      if( pkey == PKEY_PropList_FullDetails ) {
         return ::InitPropVariantFromString(L"prop:System.ItemNameDisplay;System.ItemTypeText;System.Size;System.FileAttributes;", &v);
      }      
   }
   if( pkey == PKEY_FindData ) {
      return ::InitPropVariantFromBuffer(m_pWfd, sizeof(WIN32_FIND_DATAW), &v);
   }
   if( pkey == PKEY_DescriptionID ) {
      SHDESCRIPTIONID did = { IsFolder() ? SHDID_FS_DIRECTORY : SHDID_FS_FILE, CLSID_ShellFolder };
      return ::InitPropVariantFromBuffer(&did, sizeof(did), &v);
   }
   return CNseBaseItem::GetProperty(pkey, v);
}

HRESULT CNseFileItem::SetProperty(REFPROPERTYKEY pkey, const CComPropVariant& v)
{
   if( pkey == PKEY_FileName ) {
      return ::PropVariantToString(v, m_pWfd->cFileName, lengthof(m_pWfd->cFileName));
   }
   if( pkey == PKEY_DateCreated ) {
      return ::PropVariantToFileTime(v, PSTF_UTC, &m_pWfd->ftCreationTime);
   }
   if( pkey == PKEY_DateModified ) {
      return ::PropVariantToFileTime(v, PSTF_UTC, &m_pWfd->ftLastWriteTime);
   }
   if( pkey == PKEY_DateAccessed ) {
      return ::PropVariantToFileTime(v, PSTF_UTC, &m_pWfd->ftLastAccessTime);
   }
   if( pkey == PKEY_FileAttributes ) {
      return ::PropVariantToUInt32(v, &m_pWfd->dwFileAttributes);
   }
   return CNseBaseItem::SetProperty(pkey, v);
}

/**
 * Returns whether the item is a folder or not.
 */
BOOL CNseFileItem::IsFolder()
{
   return IsBitSet(m_pWfd->dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY);
}

/**
 * Return the Shell Display flags.
 * We simply translate the file-attributes to flags.
 */
SFGAOF CNseFileItem::GetSFGAOF(SFGAOF dwMask)
{
   SFGAOF Flags = 0;
   if( IsBitSet(m_pWfd->dwFileAttributes, FILE_ATTRIBUTE_HIDDEN) ) Flags |= SFGAO_HIDDEN;
   if( IsBitSet(m_pWfd->dwFileAttributes, FILE_ATTRIBUTE_READONLY) ) Flags |= SFGAO_READONLY;
   if( IsBitSet(m_pWfd->dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY) ) Flags |= SFGAO_DROPTARGET;
   if( IsBitSet(m_pWfd->dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY) ) Flags |= SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_BROWSABLE | SFGAO_FILESYSANCESTOR;
   //if( IsBitSet(m_pWfd->dwFileAttributes, FILE_ATTRIBUTE_REPARSE_POINT) ) Flags |= SFGAO_LINK;
   // We bump to FILESYSTEM mode when entering the SaveAs hack
   if( m_pWfd->cAlternateFileName[1] == VFS_HACK_SAVEAS_JUNCTION ) Flags |= SFGAO_FILESYSTEM;
   // We support IStream for files and IStorage for folders
   if( IsBitSet(m_pWfd->dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY) ) Flags |=  SFGAO_STORAGE | SFGAO_STORAGEANCESTOR;
   else Flags |= SFGAO_STREAM;
   return Flags;
}

/**
 * Return file information.
 * We use this to return a simple structure with key information about
 * our item.
 */
VFS_FIND_DATA CNseFileItem::GetFindData()
{
   return *m_pWfd;
}

/**
 * Returns the folder settings.
 * The Folder Settings structure contains optional values for the initial
 * display of the folder.
 */
VFS_FOLDERSETTINGS CNseFileItem::GetFolderSettings()
{
   VFS_FOLDERSETTINGS Settings = { 0 };
   // HarryWu, 2014.2.15
   // Comment them, Do not allow deep search in sub folders.
   // Allow deep-search in subfolders
   Settings.FlagsValue = FWF_USESEARCHFOLDER;
   Settings.FlagsMask = FWF_USESEARCHFOLDER;
   // HarryWu, 2014.2.15
   // Use Detail by default.
   // Settings.ViewMode = FLVM_DETAILS;
   return Settings;
}

// Implementation

HRESULT CNseFileItem::_GetPathnameQuick(PCIDLIST_RELATIVE pidlPath, PCITEMID_CHILD pidlChild, LPWSTR pszPath) const
{
   ATLASSERT(ILIsAligned64(pidlPath));
   if( ::ILIsEmpty(pidlPath) ) {
      pidlPath = pidlChild;
      pidlChild = NULL;
   }
   while( !::ILIsEmpty(pidlPath) ) {
      const VFS_FIND_DATA* pWfd = &(reinterpret_cast<const NSEFILEPIDLDATA*>(pidlPath))->wfd;
      ::PathAppend(pszPath, pWfd->cFileName);
      pidlPath = static_cast<PCIDLIST_RELATIVE>(::ILNext(pidlPath));
   }
   if( !::ILIsEmpty(pidlChild) ) {
      ATLASSERT(::ILIsChild(pidlChild));
      const VFS_FIND_DATA* pWfd = &(reinterpret_cast<const NSEFILEPIDLDATA*>(pidlChild))->wfd;
      ::PathAppend(pszPath, pWfd->cFileName);
   }
   return S_OK;
}

HRESULT CNseFileItem::_GetIdQuick(PCITEMID_CHILD pidlChild, RemoteId * pdwID) const
{
	ATLASSERT(ILIsAligned64(pidlChild));
	if( !::ILIsEmpty(pidlChild) ) {
		ATLASSERT(::ILIsChild(pidlChild));
		const VFS_FIND_DATA* pWfd = &(reinterpret_cast<const NSEFILEPIDLDATA*>(pidlChild))->wfd;
		*pdwID = pWfd->dwId;
	}else{
		pdwID->category = 0; pdwID->id = 0;	 // The Root ID.
	}
	return S_OK;
}

HRESULT CNseFileItem::_GetViewIdQuick(PCIDLIST_RELATIVE pidlPath, RemoteId * pRemoteId) const
{
	ATLASSERT(ILIsAligned64(pidlPath));
	if( ::ILIsEmpty(pidlPath) ) {
		return S_FALSE;
	}
	while( !::ILIsEmpty(pidlPath) ) {
		const VFS_FIND_DATA* pWfd = &(reinterpret_cast<const NSEFILEPIDLDATA*>(pidlPath))->wfd;
		*pRemoteId = pWfd->dwId;
		pidlPath = static_cast<PCIDLIST_RELATIVE>(::ILNext(pidlPath));
	}
	return S_OK;
}
