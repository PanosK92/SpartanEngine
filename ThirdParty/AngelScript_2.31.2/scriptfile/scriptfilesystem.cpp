#include "scriptfilesystem.h"

#if defined(_WIN32)
#include <direct.h> // _getcwd
#include <Windows.h> // FindFirstFile, GetFileAttributes
#else
#include <unistd.h> // getcwd
#include <dirent.h> // opendir, readdir, closedir
#include <sys/stat.h> // stat
#endif
#include <assert.h> // assert

using namespace std;

BEGIN_AS_NAMESPACE

CScriptFileSystem *ScriptFileSystem_Factory()
{
	return new CScriptFileSystem();
}

void RegisterScriptFileSystem_Native(asIScriptEngine *engine)
{
	int r;

	r = engine->RegisterObjectType("filesystem", 0, asOBJ_REF); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("filesystem", asBEHAVE_FACTORY, "filesystem @f()", asFUNCTION(ScriptFileSystem_Factory), asCALL_CDECL); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("filesystem", asBEHAVE_ADDREF, "void f()", asMETHOD(CScriptFileSystem,AddRef), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectBehaviour("filesystem", asBEHAVE_RELEASE, "void f()", asMETHOD(CScriptFileSystem,Release), asCALL_THISCALL); assert( r >= 0 );
	
	r = engine->RegisterObjectMethod("filesystem", "bool changeCurrentPath(const string &in)", asMETHOD(CScriptFileSystem, ChangeCurrentPath), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("filesystem", "string getCurrentPath() const", asMETHOD(CScriptFileSystem, GetCurrentPath), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("filesystem", "array<string> @getDirs()", asMETHOD(CScriptFileSystem, GetDirs), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("filesystem", "array<string> @getFiles()", asMETHOD(CScriptFileSystem, GetFiles), asCALL_THISCALL); assert( r >= 0 );
	r = engine->RegisterObjectMethod("filesystem", "bool isDir(const string &in)", asMETHOD(CScriptFileSystem, IsDir), asCALL_THISCALL); assert( r >= 0 );
}

void RegisterScriptFileSystem(asIScriptEngine *engine)
{
//	if( strstr(asGetLibraryOptions(), "AS_MAX_PORTABILITY") )
//		RegisterScriptFileSystem_Generic(engine);
//	else
		RegisterScriptFileSystem_Native(engine);
}

CScriptFileSystem::CScriptFileSystem()
{
	refCount = 1;

	// Gets the application's current working directory as the starting point
	char buffer[1000];
#if defined(_WIN32)
	currentPath = _getcwd(buffer, 1000);
#else
	currentPath = getcwd(buffer, 1000);
#endif
}

CScriptFileSystem::~CScriptFileSystem()
{
}

void CScriptFileSystem::AddRef() const
{
	asAtomicInc(refCount);
}

void CScriptFileSystem::Release() const
{
	if( asAtomicDec(refCount) == 0 )
		delete this;
}

CScriptArray *CScriptFileSystem::GetFiles() const
{
	// Obtain a pointer to the engine
	asIScriptContext *ctx = asGetActiveContext();
	asIScriptEngine *engine = ctx->GetEngine();

	// TODO: This should only be done once
	// TODO: This assumes that CScriptArray was already registered
	asITypeInfo *arrayType = engine->GetTypeInfoByDecl("array<string>");

	// Create the array object
	CScriptArray *array = CScriptArray::Create(arrayType);

#if defined(_WIN32)
	// Windows uses UTF16 so it is necessary to convert the string
	wchar_t bufUTF16[10000];
	string searchPattern = currentPath + "/*";
	MultiByteToWideChar(CP_UTF8, 0, searchPattern.c_str(), -1, bufUTF16, 10000);

	WIN32_FIND_DATAW ffd;
	HANDLE hFind = FindFirstFileW(bufUTF16, &ffd);
	if( INVALID_HANDLE_VALUE == hFind ) 
		return array;
	
	do
	{
		// Skip directories
		if( (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
			continue;

		// Convert the file name back to UTF8
		char bufUTF8[10000];
		WideCharToMultiByte(CP_UTF8, 0, ffd.cFileName, -1, bufUTF8, 10000, 0, 0);
		
		// Add the file to the array
		array->Resize(array->GetSize()+1);
		((string*)(array->At(array->GetSize()-1)))->assign(bufUTF8);
	}
	while( FindNextFileW(hFind, &ffd) != 0 );

	FindClose(hFind);
#else
	dirent *ent = 0;
	DIR *dir = opendir(currentPath.c_str());
	while( (ent = readdir(dir)) != NULL ) 
	{
		const string filename = ent->d_name;

		// Skip . and ..
		if( filename[0] == '.' )
			continue;

		// Skip sub directories
		const string fullname = currentPath + "/" + filename;
		struct stat st;
		if( stat(fullname.c_str(), &st) == -1 )
			continue;
		if( (st.st_mode & S_IFDIR) != 0 )
			continue;

		// Add the file to the array
		array->Resize(array->GetSize()+1);
		((string*)(array->At(array->GetSize()-1)))->assign(filename);
	}
	closedir(dir);
#endif

	return array;
}

CScriptArray *CScriptFileSystem::GetDirs() const
{
	// Obtain a pointer to the engine
	asIScriptContext *ctx = asGetActiveContext();
	asIScriptEngine *engine = ctx->GetEngine();

	// TODO: This should only be done once
	// TODO: This assumes that CScriptArray was already registered
	asITypeInfo *arrayType = engine->GetTypeInfoByDecl("array<string>");

	// Create the array object
	CScriptArray *array = CScriptArray::Create(arrayType);

#if defined(_WIN32)
	// Windows uses UTF16 so it is necessary to convert the string
	wchar_t bufUTF16[10000];
	string searchPattern = currentPath + "/*";
	MultiByteToWideChar(CP_UTF8, 0, searchPattern.c_str(), -1, bufUTF16, 10000);
	
	WIN32_FIND_DATAW ffd;
	HANDLE hFind = FindFirstFileW(bufUTF16, &ffd);
	if( INVALID_HANDLE_VALUE == hFind ) 
		return array;
	
	do
	{
		// Skip files
		if( !(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
			continue;

		// Convert the file name back to UTF8
		char bufUTF8[10000];
		WideCharToMultiByte(CP_UTF8, 0, ffd.cFileName, -1, bufUTF8, 10000, 0, 0);

		if( strcmp(bufUTF8, ".") == 0 || strcmp(bufUTF8, "..") == 0 )
			continue;
		
		// Add the dir to the array
		array->Resize(array->GetSize()+1);
		((string*)(array->At(array->GetSize()-1)))->assign(bufUTF8);
	}
	while( FindNextFileW(hFind, &ffd) != 0 );

	FindClose(hFind);
#else
	dirent *ent = 0;
	DIR *dir = opendir(currentPath.c_str());
	while( (ent = readdir(dir)) != NULL ) 
	{
		const string filename = ent->d_name;

		// Skip . and ..
		if( filename[0] == '.' )
			continue;

		// Skip files
		const string fullname = currentPath + "/" + filename;
		struct stat st;
		if( stat(fullname.c_str(), &st) == -1 )
			continue;
		if( (st.st_mode & S_IFDIR) == 0 )
			continue;

		// Add the dir to the array
		array->Resize(array->GetSize()+1);
		((string*)(array->At(array->GetSize()-1)))->assign(filename);
	}
	closedir(dir);
#endif

	return array;
}

bool CScriptFileSystem::ChangeCurrentPath(const string &path)
{
	if( path.find(":") != string::npos || path.find("/") == 0 || path.find("\\") == 0 )
		currentPath = path;
	else
		currentPath += "/" + path;

	// Remove trailing slashes from the path
	while( currentPath.length() && (currentPath[currentPath.length()-1] == '/' || currentPath[currentPath.length()-1] == '\\') )
		currentPath.resize(currentPath.length()-1);

	return IsDir(currentPath);
}

bool CScriptFileSystem::IsDir(const string &path) const
{
	string search;
	if( path.find(":") != string::npos || path.find("/") == 0 || path.find("\\") == 0 )
		search = path;
	else
		search = currentPath + "/" + path;

#if defined(_WIN32)
	// Windows uses UTF16 so it is necessary to convert the string
	wchar_t bufUTF16[10000];
	MultiByteToWideChar(CP_UTF8, 0, search.c_str(), -1, bufUTF16, 10000);

	// Check if the path exists and is a directory
	DWORD attrib = GetFileAttributesW(bufUTF16);
	if( attrib == INVALID_FILE_ATTRIBUTES ||
		!(attrib & FILE_ATTRIBUTE_DIRECTORY) )
		return false;
#else
	// Check if the path exists and is a directory
	struct stat st;
	if( stat(search.c_str(), &st) == -1 )
		return false;
	if( (st.st_mode & S_IFDIR) == 0 )
		return false;
#endif

	return true;
}

string CScriptFileSystem::GetCurrentPath() const
{
	return currentPath;
}


END_AS_NAMESPACE
