/////////////////////////////////////////////////////////////////////////////
// TAR Folder Shell Extension
//
// Written by Bjarke Viksoe (bjarke@viksoe.dk)
// Copyright (c) 2009 Bjarke Viksoe.
//
// This code may be used in compiled form in any way you desire. This
// source file may be redistributed by any means PROVIDING it is 
// not sold for profit without the authors written consent, and 
// providing that this notice and the authors name is included. 
//
// This file is provided "as is" with no expressed or implied warranty.
// The author accepts no liability if it causes any damage to you or your
// computer whatsoever. It's free, so don't hassle me about it.
//
// Beware of bugs.
//

#pragma once

// HarryWu, 2014.1.29
// the data source implementation.
#include <datamgr.h>

#include "NseFileSystem.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions

#define TARFILE_MAGIC_ID  0xEF


///////////////////////////////////////////////////////////////////////////////
// CTarShellModule

class CTarShellModule : public CNseModule
{
public:
   // CShellModule

   LONG GetConfigInt(VFS_CONFIG Item);
   BOOL GetConfigBool(VFS_CONFIG Item);
   LPCWSTR GetConfigStr(VFS_CONFIG Item);

   HRESULT DllInstall();
   HRESULT DllUninstall();
   HRESULT ShellAction(LPCWSTR pstrType, LPCWSTR pstrCmdLine);

   BOOL DllMain(DWORD dwReason, LPVOID lpReserved);

   HRESULT CreateFileSystem(PCIDLIST_ABSOLUTE pidlRoot, CNseFileSystem** ppFS);
   HRESULT LoadLangResource();
   HRESULT CheckCallingProcess();
};


///////////////////////////////////////////////////////////////////////////////
// CTarFileSystem

class CTarFileSystem : public CNseFileSystem
{
public:
   TAR_ARCHIVE* m_pArchive;                      // Internal implementation of file-system
   volatile LONG m_cRef;                         // Reference count

   // Constructor

   CTarFileSystem();
   virtual ~CTarFileSystem();

   // Operations

   HRESULT Init(PCIDLIST_ABSOLUTE pidlRoot);

   // CShellFileSystem
   
   VOID AddRef();
   VOID Release();

   CNseItem* GenerateRoot(CShellFolder* pFolder);
};


///////////////////////////////////////////////////////////////////////////////
// CTarFileItem

class CTarFileItem : public CNseFileItem
{
public:
   CTarFileItem(CShellFolder* pFolder, PCIDLIST_RELATIVE pidlFolder, PCITEMID_CHILD pidlItem, BOOL bReleaseItem);

   // CNseFileItem

   SFGAOF GetSFGAOF(SFGAOF Mask);
   HRESULT GetProperty(REFPROPERTYKEY pkey, CComPropVariant& v);
   HRESULT SetProperty(REFPROPERTYKEY pkey, const CComPropVariant& v);

   CNseItem* GenerateChild(CShellFolder* pFolder, PCIDLIST_RELATIVE pidlFolder, PCITEMID_CHILD pidlItem, BOOL bReleaseItem);
   CNseItem* GenerateChild(CShellFolder* pFolder, PCIDLIST_RELATIVE pidlFolder, const VFS_FIND_DATA & wfd);

   HRESULT GetChild(LPCWSTR pstrName, SHGNO ParseType, CNseItem** pItem);
   HRESULT EnumChildren(HWND hwndOwner, SHCONTF grfFlags, CSimpleValArray<CNseItem*>& aList);
   
   HRESULT GetStream(const VFS_STREAM_REASON& Reason, CNseFileStream** ppFile);

   HRESULT CreateFolder();
   HRESULT Rename(LPCWSTR pstrNewName, LPWSTR pstrOutputName);
   HRESULT Delete();
   HRESULT OnShellViewCreated(HWND hWndOwner, HWND shellViewWnd, DWORD dwCat, DWORD dwId);
   HRESULT OnShellViewRefreshed(HWND hWndOwner, HWND shellViewWnd);
   HRESULT OnShellViewSized(HWND hWndOwner, HWND shellViewWnd);
   HRESULT OnShellViewClosing(HWND hWndOwner, HWND shellViewWnd);
   HRESULT InitCustomColumns();
   HRESULT SelectItems(HWND hWndOwner, HWND hShellViewWindow, LPCWSTR itemIds);
   HRESULT Resort(HWND hWndOwner, HWND hShellView, const wchar_t * sortKey, int iDirection);

   HMENU GetMenu();
   HRESULT SelectMenuItems(HWND hDefShellView, const wchar_t* idstring, MenuType * selectedMenuItems);
   HRESULT ExecuteMenuCommand(VFS_MENUCOMMAND& Cmd);

   // Implementation

   TAR_ARCHIVE* _GetTarArchivePtr() const;
   HRESULT _ExtractToFolder(VFS_MENUCOMMAND& Cmd);
   HRESULT _DoPasteFiles(VFS_MENUCOMMAND& Cmd);
   HRESULT _PreviewFile(PCITEMID_CHILD pidl);
   HRESULT _PrevPage(VFS_MENUCOMMAND & Cmd);
   HRESULT _NextPage(VFS_MENUCOMMAND & Cmd);
   HRESULT _GotoPage(VFS_MENUCOMMAND & Cmd);
   HRESULT _Share(VFS_MENUCOMMAND & Cmd);
   HRESULT _Upload(VFS_MENUCOMMAND & Cmd);
   HRESULT _InternalLink(VFS_MENUCOMMAND & Cmd);
   HRESULT _Distribute(VFS_MENUCOMMAND & Cmd);
   HRESULT _LockFile(VFS_MENUCOMMAND & Cmd, BOOL lock);
   HRESULT _HistoryVersion(VFS_MENUCOMMAND & Cmd);
   HRESULT _ViewLog(VFS_MENUCOMMAND & Cmd);
   HRESULT _ExtEdit(VFS_MENUCOMMAND & Cmd);
   HRESULT _Search(VFS_MENUCOMMAND & Cmd);
   HRESULT _Recover(VFS_MENUCOMMAND & Cmd);
   HRESULT _ClearRecycleBin(VFS_MENUCOMMAND & Cmd);
   HRESULT _DoNewFolder(VFS_MENUCOMMAND& Cmd, UINT uLabelRes);
   
   // Utility
   std::wstring GetSelectedIdList(VFS_MENUCOMMAND & Cmd);
};


///////////////////////////////////////////////////////////////////////////////
// CTarFileStream

class CTarFileStream : public CNseFileStream
{
public:
   CRefPtr<CTarFileSystem> m_spFS;               // Reference to source archive
   WCHAR m_wszFilename[MAX_PATH + 1];            // Local path to file
   LPBYTE m_pData;                               // Memory buffer
   DWORD m_dwAllocSize;                          // Allocated memory so far
   DWORD m_dwCurPos;                             // Current position in memory buffer
   DWORD m_dwFileSize;                           // Known size of file in memory
   UINT m_uAccess;                               // Stream opened for read or write access?
   RemoteId m_itemId;
   RemoteId m_parentId;

   // Constructor

   CTarFileStream(CTarFileSystem* pFS, RemoteId parentId, RemoteId itemId, LPCWSTR pstrFilename, UINT uAccess);
   virtual ~CTarFileStream();

   // CNseFileStream

   HRESULT Init();
   HRESULT Read(LPVOID pData, ULONG dwSize, ULONG& dwRead);
   HRESULT Write(LPCVOID pData, ULONG dwSize, ULONG& dwWritten);
   HRESULT Seek(DWORD dwPos);
   HRESULT GetCurPos(DWORD* pdwPos);
   HRESULT GetFileSize(DWORD* pdwFileSize);
   HRESULT SetFileSize(DWORD dwSize);
   HRESULT Commit();
   HRESULT Close();
};

