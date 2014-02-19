#pragma warning(disable:4996)
#include <Windows.h>
#include <ShlObj.h>
#include <atlbase.h>
#include <string>
#include <list>
#include <iostream>
#include <sys/stat.h>

///////////////////////////////////////////////////////////////////////////////
// Macros

#ifndef lengthof
#define lengthof(x)  (sizeof(x)/sizeof(x[0]))
#endif  // lengthof

#ifndef offsetof
#define offsetof(type, field)  ((int)&((type*)0)->field)
#endif  // offsetof

#ifndef IsBitSet
#define IsBitSet(val, bit)  (((val)&(bit))!=0)
#endif // IsBitSet


#ifdef _DEBUG
#define HR(expr)  { HRESULT _hr; if(FAILED(_hr=(expr))) { _CrtDbgReport(_CRT_ASSERT, __FILE__, __LINE__, NULL, #expr); _CrtDbgBreak(); return _hr; } }  
#else
#define HR(expr)  { HRESULT _hr; if(FAILED(_hr=(expr))) return _hr; }
#endif // _DEBUG

#include "datamgr.h"
#include "Utility.h"
#include <json/json.h>
#include <curl/curl.h>

#define SKIP_PEER_VERIFICATION
#define SKIP_HOSTNAME_VERIFICATION

Utility::Utility(void)
{
}

Utility::~Utility(void)
{
}

BOOL Utility::Login(const wchar_t * user, const wchar_t * pass, wchar_t * accessToken)
{
	// "http://192.168.253.242/EDoc2WebApi/api/Org/UserRead/UserLogin?userLoginName=admin&password=edoc2"
	wchar_t url [MaxUrlLength] = _T("");
	std::wstring strServer = _T("192.168.253.242:80");
	wsprintf(url
		, _T("http://%s/EDoc2WebApi/api/Org/UserRead/UserLogin?userLoginName=%s&password=%s")
		, strServer.c_str(), user, pass);

	std::wstring response;
	if (!HttpRequest(url, response))
	{
		return FALSE;
	}
	return TRUE;
}

BOOL Utility::GetTopPublic(const wchar_t * accessToken, std::list<RFS_FIND_DATA> & topPublic)
{
	if (accessToken == NULL || !*accessToken){
		wchar_t szToken [MaxUrlLength] = _T("");
		if (!Login(_T("admin"), _T("edoc2"), szToken))
		{
			return FALSE;
		}
		accessToken = szToken;
	}

	// "http://192.168.253.242/EDoc2WebApi/api/Doc/FolderRead/GetTopPublicFolder?token="
	wchar_t url [MaxUrlLength] = _T("");
	std::wstring strServer = _T("192.168.253.242:80");
	wsprintf(url
		, _T("http://%s/EDoc2WebApi/api/Doc/FolderRead/GetTopPublicFolder?token=%s")
		, strServer.c_str(), accessToken);

	std::wstring response;
    if (!HttpRequest(url, response))
	{
		return FALSE;
	}
	return TRUE;
}

BOOL Utility::GetTopPersonal(const wchar_t * accessToken, std::list<RFS_FIND_DATA> & topPersonal)
{
	// "http://192.168.253.242/EDoc2WebApi/api/Doc/FolderRead/GetTopPersonalFolder?token="
	return TRUE;
}

BOOL Utility::GetChildFolders(const wchar_t * accessToken, const RemoteId & folderId, std::list<RFS_FIND_DATA> & childFolders)
{
	// "http://192.168.253.242/EDoc2WebApi/api/Doc/FolderRead/GetChildFolderListByFolderId?token=
	return TRUE;
}

BOOL Utility::GetChildFiles(const wchar_t * accessToken, const RemoteId & folderId, std::list<RFS_FIND_DATA> & childFiles)
{
	// "http://192.168.253.242/EDoc2WebApi/api/Doc/FileRead/GetChildFileListByFolderId?token=
	return TRUE;
}

BOOL Utility::ConstructRecycleFolder(RFS_FIND_DATA & recycleFolder)
{
	memset(&recycleFolder, 0, sizeof(recycleFolder));
	recycleFolder.dwId.category = RecycleCat;
	recycleFolder.dwId.id = 0;
	wcscpy_s(recycleFolder.cFileName, lengthof(recycleFolder.cFileName), _T("RecycleBin"));
	recycleFolder.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
	return TRUE;
}

BOOL Utility::ConstructSearchFolder(RFS_FIND_DATA & searchFolder)
{
	memset(&searchFolder, 0, sizeof(searchFolder));
	searchFolder.dwId.category = SearchCat;
	searchFolder.dwId.id = 0;
	wcscpy_s(searchFolder.cFileName, lengthof(searchFolder.cFileName), _T("SearchBin"));
	searchFolder.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
	return TRUE;
}

bool Utility::RfsComparation(const RFS_FIND_DATA & left, const RFS_FIND_DATA & right)
{
	return (left.dwId.category - right.dwId.category);
	//return wcscmp(left.cFileName, right.cFileName);
}

struct MemoryStruct {
	char *memory;
	size_t size;
};

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;

	mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);
	if(mem->memory == NULL) {
		/* out of memory! */ 
		printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}


BOOL Utility::HttpRequest(const wchar_t * requestUrl, std::wstring & response)
{
	CURL *curl;
	CURLcode res;

	struct MemoryStruct chunk;

	chunk.memory = (char *)malloc(1);  /* will be grown as needed by the realloc above */ 
	chunk.size = 0;    /* no data at this point */ 

	curl_global_init(CURL_GLOBAL_DEFAULT);

	curl = curl_easy_init();
	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, CW2A(requestUrl));

#ifdef SKIP_PEER_VERIFICATION
		/*
		* If you want to connect to a site who isn't using a certificate that is
		* signed by one of the certs in the CA bundle you have, you can skip the
		* verification of the server's certificate. This makes the connection
		* A LOT LESS SECURE.
		*
		* If you have a CA cert for the server stored someplace else than in the
		* default bundle, then the CURLOPT_CAPATH option might come handy for
		* you.
		*/
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif

#ifdef SKIP_HOSTNAME_VERIFICATION
		/*
		* If the site you're connecting to uses a different host name that what
		* they have mentioned in their server certificate's commonName (or
		* subjectAltName) fields, libcurl will refuse to connect. You can skip
		* this check, but this will make the connection less secure.
		*/
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif

		/* send all data to this function  */ 
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

		/* we pass our 'chunk' struct to the callback function */ 
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK){
			fprintf(stderr
					,"curl_easy_perform() failed: %s\n"
					,curl_easy_strerror(res));
		}else{
			/*
			* Now, our chunk.memory points to a memory block that is chunk.size
			* bytes big and contains the remote file.
			*
			* Do something nice with it!
			*/ 
			printf("%lu bytes retrieved\n", (long)chunk.size);

			response = CA2W(chunk.memory);
		}

		/* always cleanup */
		curl_easy_cleanup(curl);
	}

	if(chunk.memory){
		free(chunk.memory);
	}

	curl_global_cleanup();

	return TRUE;
}