#ifdef _MSC_VER
#define _SCL_SECURE_NO_WARNINGS
#define	_HAS_ITERATOR_DEBUGGING	0
#define	_ITERATOR_DEBUG_LEVEL	0
#define	MYUTILS_CPP
#endif

#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <signal.h>
#include <float.h>
#include <algorithm>
#include <stdlib.h>

#if (defined(_MSC_VER) || defined(__MINGW32__))
#define WIN32_LEAN_AND_MEAN
#include <crtdbg.h>
#include <process.h>
#include <windows.h>
#include <psapi.h>
#include <io.h>
#else
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#endif

#ifndef _MSC_VER
#include <dirent.h>
#include <sys/types.h>
#endif

#include "myutils.h"

unsigned g_AllocLine;
const char *g_AllocFile;

static map<FILE *, string> FileToFileName;

const unsigned MY_IO_BUFSIZ = 32000;
const unsigned MAX_FORMATTED_STRING_LENGTH = 64000;

static char *g_IOBuffers[256];
static time_t g_StartTime = time(0);
extern vector<string> g_Argv;
static double g_PeakMemUseBytes;
static char g_TmpStr[64];
static string g_TmpStdStr;
unsigned g_AllocCount;
unsigned g_FreeCount;
const unsigned MAX_FILENO = 255;
static uint64 g_FilenoToByesRead[MAX_FILENO+1];
static uint64 g_BytesIOSinceLastProgressTick;
static const uint64 PROGRESS_TICK_BYTES = 16*1024*1024;

static void IncIO(FILE *f, uint64 Bytes)
	{
	int fn = fileno(f);
	if (fn >= 0 && fn <= MAX_FILENO)
		g_FilenoToByesRead[fn] += Bytes;
	g_BytesIOSinceLastProgressTick += Bytes;
	if (g_BytesIOSinceLastProgressTick >= PROGRESS_TICK_BYTES)
		{
		Progress_OnTick();
		g_BytesIOSinceLastProgressTick = 0;
		}
	}

#if ALLOC_TOTALS
uint64 g_AllocTotal;
uint64 g_FreeTotal;
#endif

const char *SVN_VERSION =
#include "svnversion.h"
;

static const double LOG2 = log(2.0);
static const double LOG10 = log(10.0);

#ifdef _MSC_VER
void OSInit()
	{
	_putenv("TZ=");

	DWORD OldState = SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_CONTINUOUS | ES_AWAYMODE_REQUIRED);
	if (OldState == NULL)
		fprintf(stderr,
		  "\n\nWarning: SetThreadExecutionState=NULL\n"
		  "Computer may go to sleep while " PROGRAM_NAME " is running\n\n");
	}
#else
void OSInit() {}
#endif

double mylog2(double x)
	{
	return log(x)/LOG2;
	}

double mylog10(double x)
	{
	return log(x)/LOG10;
	}

unsigned GetThreadCount()
	{
	return omp_get_num_threads();
	}

unsigned GetRequestedThreadCount()
	{
	static unsigned N = 1;
	static bool Done = false;
	if (Done)
		return N;
	unsigned MaxN = omp_get_max_threads();
	unsigned CoreCount = GetCPUCoreCount();
	if (optset_threads)
		N = opt(threads);
	else
		{
		if (CoreCount > 10)
			{
			Progress("CPU has %u cores, defaulting to 10 threads\n", CoreCount);
			N = 10;
			}
		else
			N = CoreCount;
		}
	if (N > MaxN)
		{
		Warning("Max OMP threads %u", MaxN);
		N = MaxN;
		}
	if (N == 0)
		N = 1;
	Done = true;
	return N;
	}

const char *GetPlatform()
	{
#if	BITS==32
	asserta(sizeof(void *) == 4);
#ifdef _MSC_VER
	return "win32";
#elif defined(__APPLE__)
	return "i86osx32";
#elif defined(__GNUC__)
	return "i86linux32";
#else
#error "Unknown compiler"
#endif
#elif BITS==64
	asserta(sizeof(void *) == 8);
#ifdef _MSC_VER
	return "win64";
#elif defined(__APPLE__)
	return "i86osx64";
#elif defined(__GNUC__)
	return "i86linux64";
#else
#error "Unknown compiler"
#endif
#else
#error "Bad BITS"
#endif
	}

const char *BaseName(const char *PathName)
	{
	const char *q = 0;
	for (const char *p = PathName; *p; ++p)
		{
		if (*p == '/' || *p == '\\')
			q = p + 1;
		}
	if (q != 0)
		return q;
	return PathName;
	}

void BaseName(const string &PathName, string &FileName)
	{
	if (PathName == "-" || PathName == "/dev/stdin")
		FileName = "(stdin)";
	else
		FileName = string(BaseName(PathName.c_str()));
	}

static void AllocBuffer(FILE *f)
	{
#if	DEBUG
	setbuf(f, 0);
#else
	int fd = fileno(f);
	if (fd < 0 || fd >= 256)
		return;
	if (g_IOBuffers[fd] == 0)
		g_IOBuffers[fd] = myalloc(char, MY_IO_BUFSIZ);
	setvbuf(f, g_IOBuffers[fd], _IOFBF, MY_IO_BUFSIZ);
#endif
	}

static void FreeBuffer(FILE *f)
	{
#if	0
	int fd = fileno(f);
	if (fd < 0 || fd >= 256)
		return;
	if (g_IOBuffers[fd] == 0)
		return;
	myfree(g_IOBuffers[fd]);
	g_IOBuffers[fd] = 0;
#endif
	}

unsigned GetElapsedSecs()
	{
	return (unsigned) (time(0) - g_StartTime);
	}

bool StdioFileExists(const string &FileName)
	{
	struct stat SD;
	int i = stat(FileName.c_str(), &SD);
	return i == 0;
	}

void myassertfail(const char *Exp, const char *File, unsigned Line)
	{
	Die("%s(%u) assert failed: %s", File, Line, Exp);
	}

bool myisatty(int fd)
	{
	return isatty(fd) != 0;
	}

#ifdef _MSC_VER
#include <io.h>
int fseeko(FILE *stream, off_t offset, int whence)
	{
	off_t FilePos = _fseeki64(stream, offset, whence);
	return (FilePos == -1L) ? -1 : 0;
	}
#define ftello(fm) (off_t) _ftelli64(fm)
#endif

void LogStdioFileState(FILE *f)
	{
	unsigned long tellpos = (unsigned long) ftello(f);
	long fseek_pos = fseek(f, 0, SEEK_CUR);
	int fd = fileno(f);
	Log("FILE *     %p\n", f);
	Log("fileno     %d\n", fd);
	Log("feof       %d\n", feof(f));
	Log("ferror     %d\n", ferror(f));
	Log("ftell      %ld\n", tellpos);
	Log("fseek      %ld\n", fseek_pos);
#if	!defined(_GNU_SOURCE) && !defined(__APPLE_CC__)
	fpos_t fpos;
	int fgetpos_retval = fgetpos(f, &fpos);
	Log("fpos       %ld (retval %d)\n", (long) fpos, fgetpos_retval);
//	Log("eof        %d\n", _eof(fd));
#endif
#ifdef _MSC_VER
	__int64 pos64 = _ftelli64(f);
	Log("_ftelli64  %lld\n", pos64);
#endif
	if (FileToFileName.find(f) == FileToFileName.end())
		Log("Not found in FileToFileName\n");
	else
		Log("Name       %s\n", FileToFileName[f].c_str());
	}

void ParseFileName(const string &FileName, string &Path, string &Name)
	{
	size_t n = string::npos;
	size_t n1 = FileName.rfind('/');
	if (n1 != string::npos)
		n = n1;

#if _MSC_VER
	size_t n2 = FileName.rfind('\\');
	size_t n3 = FileName.rfind(':');
	if (n2 != string::npos && n2 > n)
		n = n2;
	if (n3 != string::npos && n3 > n)
		n = n3;
#endif

	if (n == string::npos)
		{
		Path = ".";
		Name = FileName;
		return;
		}

	Path = FileName.substr(0, n);
	Name = FileName.substr(n+1, string::npos);
	}

#ifdef _MSC_VER
void ReadDir(const string &DirName, vector<string> &FileNames)
	{
	FileNames.clear();

	if (DirName.find('?') != string::npos || DirName.find('*') != string::npos)
		Die("Invalid directory name '%s'", DirName.c_str());

	string DirNameSlashStar = DirName;
	if (!EndsWith(DirName, "/") && !EndsWith(DirName, "\\"))
		DirNameSlashStar += "/";
	DirNameSlashStar += "*";
	struct _finddata_t FileInfo;
	intptr_t h = _findfirst(DirNameSlashStar.c_str(), &FileInfo);
	if (h == -1)
		Die("Directory not found '%s'", DirName.c_str());
	for (;;)
		{
		string FileName = string(FileInfo.name);
		FileNames.push_back(FileName);
		int rc = _findnext(h, &FileInfo);
		if (rc != 0)
			break;
		FileNames.push_back(FileInfo.name);
		}
	_findclose(h);
	sort(FileNames.begin(), FileNames.end());
	}

#else

void ReadDir(const string &DirName, vector<string> &FileNames)
	{
	FileNames.clear();

	DIR *h = opendir(DirName.c_str());
	if (h == 0)
		Die("Directory not found '%s'", DirName.c_str());
	for (;;)
		{
		struct dirent *d = readdir(h);
		if (d == 0)
			break;
		string FileName = string(d->d_name);
		FileNames.push_back(FileName);
		}
	closedir(h);
	sort(FileNames.begin(), FileNames.end());
	}

#endif

static void ReadStdioFile_Raw(FILE *f, void *Buffer, uint64 Bytes)
	{
	asserta(f != 0);
	uint64 BytesLeft = Bytes;
	uint64 BufferPos = 0;
	const unsigned ChunkSize = 32*1024*1024;
	for (;;)
		{
		if (BytesLeft == 0)
			break;
		uint64 BytesToRead = BytesLeft;
		if (BytesToRead > ChunkSize)
			BytesToRead = ChunkSize;
		byte *ptrBuffer = (byte *) Buffer + BufferPos;
		size_t ElementsRead = fread(ptrBuffer, BytesToRead, 1, f);
		if (ElementsRead != 1)
			{
			LogStdioFileState(f);
			Die("File read failed, errno %d", errno);
			}
		IncIO(f, BytesToRead);
		BufferPos += BytesToRead;
		BytesLeft -= BytesToRead;
		}
	}

static void WriteStdioFile_Raw(FILE *f, const void *Buffer, uint64 Bytes)
	{
	asserta(f != 0);
	uint64 BytesLeft = Bytes;
	uint64 BufferPos = 0;
	const unsigned ChunkSize = 32*1024*1024;
	for (;;)
		{
		if (BytesLeft == 0)
			break;
		uint64 BytesToWrite = BytesLeft;
		if (BytesToWrite > ChunkSize)
			BytesToWrite = ChunkSize;
		byte *ptrBuffer = (byte *) Buffer + BufferPos;
		size_t ElementsWritten = fwrite(ptrBuffer, BytesToWrite, 1, f);
		if (ElementsWritten != 1)
			{
			LogStdioFileState(f);
			Die("File write failed errno=%d", int(errno));
			}
		IncIO(f, BytesToWrite);
		BufferPos += BytesToWrite;
		BytesLeft -= BytesToWrite;
		}
	}

void ReadStdioFile(FILE *f, void *Buffer, uint32 Bytes)
	{
	ReadStdioFile_Raw(f, Buffer, Bytes);
	}

FILE *OpenStdioFile(const string &FileName)
	{
	if (FileName == "")
		Die("Missing input file name");
	if (FileName == "-")
		return stdin;

#if _MSC_VER		
	if (FileName == "/dev/stdin")
		return stdin;
#endif

	const char *Mode = "rb";
	FILE *f = fopen(FileName.c_str(), Mode);
	if (f == 0)
		Die("Cannot open %s, errno=%d %s",
		  FileName.c_str(), errno, strerror(errno));
	AllocBuffer(f);
	FileToFileName[f] = FileName;
	return f;
	}

FILE *CreateStdioFile(const string &FileName)
	{
	if (FileName == "")
		// Die("Missing output file name");
		return 0;

	if (FileName == "-")
		return stdout;

#if _MSC_VER		
	if (FileName == "/dev/stdout")
		return stdout;
#endif

	FILE *f = fopen(FileName.c_str(), "wb+");
	if (0 == f)
		Die("Cannot create %s, errno=%d %s",
		  FileName.c_str(), errno, strerror(errno));
	const unsigned MYBUFFSZ = 262144+8;
	char *buf = (char *) malloc(MYBUFFSZ);
	setvbuf(f, buf, _IOFBF, MYBUFFSZ);   
	AllocBuffer(f);
	FileToFileName[f] = FileName;
	return f;
	}

void SetStdioFilePos(FILE *f, uint32 Pos)
	{
	if (0 == f)
		Die("SetStdioFilePos failed, f=NULL");
	int Ok = fseeko(f, Pos, SEEK_SET);
	off_t NewPos = ftello(f);
	if (Ok != 0 || Pos != NewPos)
		{
		LogStdioFileState(f);
		Die("SetStdioFilePos(%d) failed, Ok=%d NewPos=%d",
		  (int) Pos, Ok, (int) NewPos);
		}
	}

void SetStdioFilePos64(FILE *f, uint64 Pos)
	{
	if (0 == f)
		Die("SetStdioFilePos failed, f=NULL");
	int Ok = fseeko(f, Pos, SEEK_SET);
	off_t NewPos = ftello(f);
	if (Ok != 0 || Pos != NewPos)
		{
		LogStdioFileState(f);
		Die("SetStdioFilePos64(%ul) failed, Ok=%d NewPos=%ul",
		  (unsigned long) Pos, Ok, (unsigned long)  NewPos);
		}
	}

void ReadStdioFile(FILE *f, uint32 Pos, void *Buffer, uint32 Bytes)
	{
	asserta(f != 0);
	SetStdioFilePos(f, Pos);
	ReadStdioFile_Raw(f, Buffer, Bytes);
	}

void ReadStdioFile64(FILE *f, uint64 Pos, void *Buffer, uint64 Bytes)
	{
	asserta(f != 0);
	SetStdioFilePos64(f, Pos);
	ReadStdioFile_Raw(f, Buffer, Bytes);
	}

uint32 ReadStdioFile_NoFail(FILE *f, void *Buffer, uint32 Bytes)
	{
	asserta(f != 0);
	off_t PosBefore = ftello(f);
	size_t ElementsRead = fread(Buffer, Bytes, 1, f);
	off_t PosAfter = ftello(f);
	if (ElementsRead == 1)
		{
		IncIO(f, Bytes);
		return Bytes;
		}
	uint32 BytesRead = uint32(PosAfter - PosBefore);
	IncIO(f, BytesRead);
	return BytesRead;
	}

void ReadStdioFile64(FILE *f, void *Buffer, uint64 Bytes)
	{
	ReadStdioFile_Raw(f, Buffer, Bytes);
	}

void WriteStdioFile(FILE *f, uint32 Pos, const void *Buffer, uint32 Bytes)
	{
	asserta(f != 0);
	SetStdioFilePos(f, Pos);
	WriteStdioFile_Raw(f, Buffer, Bytes);
	}

void WriteStdioFileStr(FILE *f, const char *s)
	{
	uint32 Bytes = ustrlen(s);
	WriteStdioFile_Raw(f, s, Bytes);
	}

void WriteStdioFile(FILE *f, const void *Buffer, uint32 Bytes)
	{
	WriteStdioFile_Raw(f, Buffer, Bytes);
	}

void WriteStdioFile64(FILE *f, const void *Buffer, uint64 Bytes)
	{
	WriteStdioFile_Raw(f, Buffer, Bytes);
	}

// Return false on EOF, true if line successfully read.
bool ReadLineStdioFile(FILE *f, char *Line, uint32 Bytes)
	{
	if (feof(f))
		return false;
	if ((int) Bytes < 0)
		Die("ReadLineStdioFile: Bytes < 0");
	off_t PosBefore = ftello(f);
	char *RetVal = fgets(Line, (int) Bytes, f);
	off_t PosAfter = ftello(f);
	uint64 BytesRead = PosAfter - PosBefore;
	IncIO(f, BytesRead);

	if (NULL == RetVal)
		{
		if (feof(f))
			return false;
		if (ferror(f))
			Die("ReadLineStdioFile: errno=%d", errno);
		Die("ReadLineStdioFile: fgets=0, feof=0, ferror=0");
		}

	if (RetVal != Line)
		Die("ReadLineStdioFile: fgets != Buffer");
	size_t n = strlen(Line);
	if (n < 1 || Line[n-1] != '\n')
		Die("ReadLineStdioFile: line too long or missing end-of-line");
	if (n > 0 && (Line[n-1] == '\r' || Line[n-1] == '\n'))
		Line[n-1] = 0;
	if (n > 1 && (Line[n-2] == '\r' || Line[n-2] == '\n'))
		Line[n-2] = 0;
	return true;
	}

void ReadTabbedLineStdioFile(FILE *f, vector<string> &Fields, unsigned FieldCount)
	{
	string Line;
	bool Ok = ReadLineStdioFile(f, Line);
	if (!Ok)
		Die("Unxpected end-of-file in tabbed text");
	Split(Line, Fields, '\t');
	unsigned n = SIZE(Fields);
	if (FieldCount != UINT_MAX && n != FieldCount)
		{
		Log("\n");
		Log("Line='%s'\n", Line.c_str());
		Die("Expected %u tabbed fields, got %u", FieldCount, n);
		}
	}

// Return false on EOF, true if line successfully read.
bool ReadLineStdioFile(FILE *f, string &Line)
	{
	Line.clear();
	off_t PosBefore = ftello(f);
	for (;;)
		{
		int c = fgetc(f);
		if (c == -1)
			{
			if (feof(f))
				{
				if (!Line.empty())
					return true;
				return false;
				}
			Die("ReadLineStdioFile, errno=%d", errno);
			}
		if (c == '\r')
			continue;
		if (c == '\n')
			{
			off_t PosAfter = ftello(f);
			IncIO(f, PosAfter - PosBefore);
			return true;
			}
		Line.push_back((char) c);
		}
	}

void RenameStdioFile(const string &FileNameFrom, const string &FileNameTo)
	{
	int Ok = rename(FileNameFrom.c_str(), FileNameTo.c_str());
	if (Ok != 0)
		Die("RenameStdioFile(%s,%s) failed, errno=%d %s",
		  FileNameFrom.c_str(), FileNameTo.c_str(), errno, strerror(errno));
	}

void FlushStdioFile(FILE *f)
	{
	int Ok = fflush(f);
	if (Ok != 0)
		Die("fflush(%p)=%d,", f, Ok);
	}

void CloseStdioFile_(FILE **f)
	{
	if (f == 0 || *f == 0)
		return;
	int Ok = fclose(*f);
	if (Ok != 0)
		Die("fclose(%p)=%d", *f, Ok);
	FreeBuffer(*f);
	f = 0;
	}

uint32 GetStdioFilePos32(FILE *f)
	{
	off_t FilePos = ftello(f);
	if (FilePos < 0)
		Die("GetStdioFilePos32 ftello=%d", (int) FilePos);
	if (FilePos > UINT32_MAX)
		Die("File offset too big for 32-bit version (%s)", MemBytesToStr((double) FilePos));
	return (uint32) FilePos;
	}

uint64 GetStdioFilePos64(FILE *f)
	{
	off_t FilePos = ftello(f);
	if (FilePos < 0)
		Die("GetStdioFilePos64 ftello=%d", (int) FilePos);
	return (uint64) FilePos;
	}

uint64 GetStdioFilePos64_NoFail(FILE *f)
	{
	off_t FilePos = ftello(f);
	if (FilePos < 0)
		return UINT64_MAX;
	return (uint64) FilePos;
	}

uint32 GetStdioFilePos32_NoFail(FILE *f)
	{
	off_t FilePos = ftello(f);
	if (FilePos < 0 || FilePos > off_t(UINT32_MAX))
		return UINT32_MAX;
	return (uint32) FilePos;
	}

uint64 GetStdioFileBytesRead(FILE *f)
	{
	int fn = fileno(f);
	if (fn >= 0 && fn <= MAX_FILENO)
		return g_FilenoToByesRead[fn];
	return 0;
	}

uint32 GetStdioFileSize32(FILE *f)
	{
	uint32 CurrentPos = GetStdioFilePos32(f);
	int Ok = fseeko(f, 0, SEEK_END);
	if (Ok < 0)
		Die("fseek in GetFileSize");

	off_t Length = ftello(f);
	SetStdioFilePos(f, CurrentPos);

	if (Length < 0)
		Die("ftello in GetFileSize");
#if	BITS == 32
	if (Length > UINT32_MAX)
		Die("File size too big for 32-bit version (%s)", MemBytesToStr((double) Length));
#endif
	return (uint32) Length;
	}

uint32 GetStdioFileSize32_NoFail(FILE *f)
	{
	uint32 CurrentPos = GetStdioFilePos32(f);
	int Ok = fseeko(f, 0, SEEK_END);
	if (Ok < 0)
		return UINT32_MAX;

	off_t Length = ftello(f);
	SetStdioFilePos(f, CurrentPos);

	if (Length < 0 || Length > off_t(UINT32_MAX))
		return UINT32_MAX;
	return (uint32) Length;
	}

uint64 GetStdioFileSize64(FILE *f)
	{
	uint64 CurrentPos = GetStdioFilePos64(f);
	int Ok = fseeko(f, 0, SEEK_END);
	if (Ok < 0)
		Die("fseek in GetFileSize64");

	off_t Length = ftello(f);
	SetStdioFilePos64(f, CurrentPos);

	if (Length < 0)
		Die("ftello in GetFileSize");
#if	BITS == 32
	if (Length > UINT32_MAX)
		Die("File size too big for 32-bit version (%s)", MemBytesToStr((double) Length));
#endif
	return (uint64) Length;
	}

uint64 GetStdioFileSize64_NoFail(FILE *f)
	{
	uint64 CurrentPos = GetStdioFilePos64_NoFail(f);
	int Ok = fseeko(f, 0, SEEK_END);
	if (Ok < 0)
		return UINT64_MAX;

	off_t Length = ftello(f);
	SetStdioFilePos64(f, CurrentPos);

	if (Length < 0)
		return UINT64_MAX;

	return (uint64) Length;
	}

void MoveStdioFile(const string &FileName1, const string &FileName2)
	{
	if (StdioFileExists(FileName2))
		DeleteStdioFile(FileName2);
	RenameStdioFile(FileName1, FileName2);
	}

void DeleteStdioFile(const string &FileName)
	{
	int Ok = remove(FileName.c_str());
	if (Ok != 0)
		Die("remove(%s) failed, errno=%d %s", FileName.c_str(), errno, strerror(errno));
	}

double GetUsableMemBytes()
	{
	double RAM = GetPhysMemBytes();
#if	BITS==32
#ifdef	_MSC_VER
	if (RAM > 2e9)
		return 2e9;
#else
	if (RAM > 4e9)
		return 4e9;
#endif
#endif
	return RAM;
	}

static char **g_ThreadStrs;
static unsigned g_ThreadStrCount;

static char *GetThreadStr()
	{
	unsigned ThreadIndex = GetThreadIndex();
	if (ThreadIndex >= g_ThreadStrCount)
		{
		unsigned NewThreadStrCount = ThreadIndex + 4;
		char **NewThreadStrs = myalloc(char *, NewThreadStrCount);
		memset_zero(NewThreadStrs, NewThreadStrCount);
		if (g_ThreadStrCount > 0)
			memcpy(NewThreadStrs, g_ThreadStrs, g_ThreadStrCount*sizeof(char *));
		g_ThreadStrs = NewThreadStrs;
		g_ThreadStrCount = NewThreadStrCount;
		}
	if (g_ThreadStrs[ThreadIndex] == 0)
		g_ThreadStrs[ThreadIndex] = myalloc(char, MAX_FORMATTED_STRING_LENGTH+1);
	char *Str = g_ThreadStrs[ThreadIndex];
	return Str;
	}

void myvstrprintf(string &Str, const char *Format, va_list ArgList)
	{
	char *szStr = GetThreadStr();
	vsnprintf(szStr, MAX_FORMATTED_STRING_LENGTH-1, Format, ArgList);
	szStr[MAX_FORMATTED_STRING_LENGTH - 1] = '\0';
	Str.assign(szStr);
	}

void Pf(FILE *f, const char *Format, ...)
	{
	if (f == 0)
		return;
	va_list ArgList;
	va_start(ArgList, Format);
	vfprintf(f, Format, ArgList);
	va_end(ArgList);
	}

void Ps(string &Str, const char *Format, ...)
	{
	va_list ArgList;
	va_start(ArgList, Format);
	myvstrprintf(Str, Format, ArgList);
	va_end(ArgList);
	}

void Psa(string &Str, const char *Format, ...)
	{
	va_list ArgList;
	va_start(ArgList, Format);
	string Tmp;
	myvstrprintf(Tmp, Format, ArgList);
	va_end(ArgList);
	Str += Tmp;
	}

void AppendIntToStr(string &Str, unsigned i)
	{
	do
		{
		Str += ('0' + i%10);
		i /= 10;
		}
	while (i != 0);
	}

void Psasc(string &Str, const char *Format, ...)
	{
	unsigned n = SIZE(Str);
	if (n > 0 && Str[n-1] != ';')
		Str += ";";

	va_list ArgList;
	va_start(ArgList, Format);
	string Tmp;
	myvstrprintf(Tmp, Format, ArgList);
	va_end(ArgList);
	Str += Tmp;
	n = SIZE(Str);
	if (n > 0 && Str[n-1] != ';')
		Str += ";";
	}

FILE *g_fLog = 0;

void SetLogFileName(const string &FileName)
	{
	if (g_fLog != 0)
		CloseStdioFile(g_fLog);
	g_fLog = 0;
	if (FileName.empty())
		return;
	g_fLog = CreateStdioFile(FileName);
	setbuf(g_fLog, 0);
	}

void Log(const char *Format, ...)
	{
	if (g_fLog == 0)
		return;
	va_list ArgList;
	va_start(ArgList, Format);
	vfprintf(g_fLog, Format, ArgList);
	va_end(ArgList);
	fflush(g_fLog);
	}

void Die_(const char *Format, ...)
	{
#pragma omp critical
	{
	static bool InDie = false;
	if (InDie)
		exit(1);
	InDie = true;
	string Msg;

	if (g_fLog != 0)
		setbuf(g_fLog, 0);
	va_list ArgList;
	va_start(ArgList, Format);
	myvstrprintf(Msg, Format, ArgList);
	va_end(ArgList);

	fprintf(stderr, "\n\n");
	Log("\n");
	time_t t = time(0);
	Log("%s", asctime(localtime(&t)));
	for (unsigned i = 0; i < g_Argv.size(); i++)
		{
		fprintf(stderr, (i == 0) ? "%s" : " %s", g_Argv[i].c_str());
		Log((i == 0) ? "%s" : " %s", g_Argv[i].c_str());
		}
	fprintf(stderr, "\n");
	Log("\n");

	time_t CurrentTime = time(0);
	unsigned ElapsedSeconds = unsigned(CurrentTime - g_StartTime);
	const char *sstr = SecsToStr(ElapsedSeconds);
	Log("Elapsed time: %s\n", sstr);

	const char *szStr = Msg.c_str();
	fprintf(stderr, "Elapsed time %s\n", SecsToHHMMSS((int) ElapsedSeconds));
	fprintf(stderr, "Max memory %s\n", MemBytesToStr(g_PeakMemUseBytes));
	fprintf(stderr, "\n---Fatal error---\n%s\n", szStr);
	Log("\n---Fatal error---\n%s\n", szStr);

#ifdef _MSC_VER
	if (IsDebuggerPresent())
 		__debugbreak();
	_CrtSetDbgFlag(0);
#endif

	exit(1);
	}
	}

void Warning_(const char *Format, ...)
	{
	string Msg;

	va_list ArgList;
	va_start(ArgList, Format);
	myvstrprintf(Msg, Format, ArgList);
	va_end(ArgList);

	const char *szStr = Msg.c_str();

	fprintf(stderr, "\nWARNING: %s\n\n", szStr);
	if (g_fLog != stdout)
		{
		Log("\nWARNING: %s\n", szStr);
		fflush(g_fLog);
		}
	}

#if (defined(_MSC_VER) || defined(__MINGW32__))
void mysleep(unsigned ms)
	{
	Sleep(ms);
	}
#else
void mysleep(unsigned ms)
	{
	usleep(1000*ms);
	}
#endif

#if (defined(_MSC_VER) || defined(__MINGW32__))
double GetMemUseBytes()
	{
	HANDLE hProc = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS PMC;
	BOOL bOk = GetProcessMemoryInfo(hProc, &PMC, sizeof(PMC));
	if (!bOk)
		return 1000000;
	double Bytes = (double) PMC.WorkingSetSize;
	if (Bytes > g_PeakMemUseBytes)
		g_PeakMemUseBytes = Bytes;
	return Bytes;
	}

double GetPhysMemBytes()
	{
	MEMORYSTATUSEX MS;
	MS.dwLength = sizeof(MS);
	BOOL Ok = GlobalMemoryStatusEx(&MS);
	if (!Ok)
		return 0.0;
	return double(MS.ullTotalPhys);
	}

#elif	linux || __linux__ || __CYGWIN__
double GetPhysMemBytes()
	{
	int fd = open("/proc/meminfo", O_RDONLY);
	if (fd < 0)
		return 0.0;
// MemTotal:       255908 kB
	char Line[128];
	int n = read(fd, Line, sizeof(Line));
	if (n < 0)
		return 0.0;
	Line[127] = 0;
	unsigned kb;
	n = sscanf(Line, "MemTotal: %u", &kb);
	if (n != 1)
		return 0.0;
	return double(kb)*1000.0;
	}

double GetMemUseBytes()
	{
	static char statm[64];
	static int PageSize = 1;
	if (0 == statm[0])
		{
		PageSize = sysconf(_SC_PAGESIZE);
		pid_t pid = getpid();
		sprintf(statm, "/proc/%d/statm", (int) pid);
		}

	int fd = open(statm, O_RDONLY);
	if (fd < 0)
		return 0.0;
	char Buffer[64];
	int n = read(fd, Buffer, sizeof(Buffer) - 1);
	close(fd);
	fd = -1;

	if (n <= 0)
		return 0.0;

	Buffer[n] = 0;
	double Pages = atof(Buffer);

	double Bytes = Pages*PageSize;
	if (Bytes > g_PeakMemUseBytes)
		g_PeakMemUseBytes = Bytes;
	return Bytes;
	}

#elif defined(__MACH__)
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/gmon.h>
#include <mach/vm_param.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <sys/vmmeter.h>
#include <sys/proc.h>
// #include <mach/task_info.h>
#include <mach/task.h>
#include <mach/mach_init.h>
#include <mach/vm_statistics.h>

#define DEFAULT_MEM_USE	0.0

double GetMemUseBytes()
	{
	task_t mytask = mach_task_self();
	struct task_basic_info ti;
	memset((void *) &ti, 0, sizeof(ti));
	mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
	kern_return_t ok = task_info(mytask, TASK_BASIC_INFO, (task_info_t) &ti, &count);
	if (ok == KERN_INVALID_ARGUMENT)
		return DEFAULT_MEM_USE;

	if (ok != KERN_SUCCESS)
		return DEFAULT_MEM_USE;

	double Bytes = (double ) ti.resident_size;
	if (Bytes > g_PeakMemUseBytes)
		g_PeakMemUseBytes = Bytes;
	return Bytes;
	}

double GetPhysMemBytes()
	{
	uint64_t mempages = 0;
	size_t len = sizeof(mempages);
	int rc = sysctlbyname("hw.memsize", &mempages, &len, NULL, 0);
	if (rc < 0)
		return 0.0;
	return double(mempages);
	}
#else
double GetMemUseBytes()
	{
	return 0.0;
	}
#endif

#ifdef _MSC_VER
void mylistdir(const string &DirName, vector<string> &FileNames)
	{
	FileNames.clear();
	bool First = true;
	HANDLE h = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATA FFD;
	for (;;)
		{
		if (First)
			{
			string s = DirName + string("/*");
			h = FindFirstFile(s.c_str(), &FFD);
			if (h == INVALID_HANDLE_VALUE)
				return;
			First = false;
			}
		else
			{
			BOOL Ok = FindNextFile(h, &FFD);
			if (!Ok)
				return;
			}
		FileNames.push_back(string(FFD.cFileName));
		}
	}
#else
void mylistdir(const string &DirName, vector<string> &FileNames)
	{
	FileNames.clear();
	DIR *dir = opendir(DirName.c_str());
	if (dir == 0)
		Die("Directory not found: %s", DirName.c_str());
	for (;;)
		{
		struct dirent *dp = readdir(dir);
		if (dp == 0)
			break;
		FileNames.push_back(string(dp->d_name));
		}
	closedir(dir);
	}
#endif

double GetPeakMemUseBytes()
	{
	return g_PeakMemUseBytes;
	}

const char *SecsToHHMMSS(int Secs)
	{
	int HH = Secs/3600;
	int MM = (Secs - HH*3600)/60;
	int SS = Secs%60;
	if (HH == 0)
		sprintf(g_TmpStr, "%02d:%02d", MM, SS);
	else
		sprintf(g_TmpStr, "%02d:%02d:%02d", HH, MM, SS);
	return g_TmpStr;
	}

const char *GetResourceStr()
	{
	double Bytes = GetMemUseBytes();
	unsigned Secs = GetElapsedSecs();
	g_TmpStdStr = string(SecsToHHMMSS(Secs));
	if (Bytes > 0)
		{
		g_TmpStdStr.push_back(' ');
		char Str[32];
		sprintf(Str, "%s", MemBytesToStr(Bytes));
		g_TmpStdStr += string(Str);
		}
	return g_TmpStdStr.c_str();
	}

const char *SecsToStr(double Secs)
	{
	if (Secs >= 60.0)
		return SecsToHHMMSS((int) Secs);

	if (Secs < 1e-6)
		sprintf(g_TmpStr, "%.2gs", Secs);
	else if (Secs < 1e-3)
		sprintf(g_TmpStr, "%.2fms", Secs*1e3);
	else if (Secs < 1.0)
		sprintf(g_TmpStr, "%.3fs", Secs);
	else if (Secs < 10.0)
		sprintf(g_TmpStr, "%.2fs", Secs);
	else
		sprintf(g_TmpStr, "%.1fs", Secs);
	return g_TmpStr;
	}

const char *MemBytesToStr(double Bytes)
	{
	if (Bytes < 1e4)
		sprintf(g_TmpStr, "%.1fb", Bytes);
	else if (Bytes < 1e6)
		sprintf(g_TmpStr, "%.1fkb", Bytes/1e3);
	else if (Bytes < 10e6)
		sprintf(g_TmpStr, "%.1fMb", Bytes/1e6);
	else if (Bytes < 1e9)
		sprintf(g_TmpStr, "%.0fMb", Bytes/1e6);
	else if (Bytes < 100e9)
		sprintf(g_TmpStr, "%.1fGb", Bytes/1e9);
	else
		sprintf(g_TmpStr, "%.0fGb", Bytes/1e9);
	return g_TmpStr;
	}

bool IsValidFloatStr(const char *s)
	{
	char *p = 0;
	double d = strtod(s, &p);
	bool Bad = (p == 0 || *p != 0);
	return !Bad;
	}

bool IsValidFloatStr(const string &s)
	{
	return IsValidFloatStr(s.c_str());
	}

double StrToFloat(const string &s, bool StarIsDblMax)
	{
	return StrToFloat(s.c_str(), StarIsDblMax);
	}

double StrToFloat(const char *s, bool StarIsDblMax)
	{
	if (StarIsDblMax && s[0] == '*' && s[1] == 0)
		return DBL_MAX;
	if (!IsValidFloatStr(s))
		Die("Invalid floating-point number '%s'", s);
	return atof(s);
	}

double StrToMemBytes(const string &s)
	{
	unsigned n = SIZE(s);
	if (n == 0)
		return 0.0;

	double d = StrToFloat(s.c_str());
	char c = toupper(s[n-1]);
	if (isdigit(c))
		return d;
	else if (c == 'K')
		return 1000.0*d;
	else if (c == 'M')
		return 1e6*d;
	else if (c == 'G')
		return 1e9*d;
	else
		Die("Invalid amount of memory '%s'", s.c_str());
	return 0.0;
	}

bool Replace(string &s, const string &a, const string &b)
	{
	size_t n = s.find(a);
	if (n == string::npos)
		return false;

	string t;
	for (size_t i = 0; i < n; ++i)
		t += s[i];

	size_t m = a.size();
	for (size_t i = n + m; i < n; ++i)
		t += s[i];
	s = t;
	return true;
	}

bool EndsWith(const string &s, const string &t)
	{
	unsigned n = SIZE(s);
	unsigned m = SIZE(t);
	if (n < m)
		return false;
	for (unsigned i = 0; i < m; ++i)
		if (s[n-i-1] != t[m-i-1])
			return false;
	return true;
	}

bool IsUintStr(const char *s)
	{
	if (!isdigit(*s++))
		return false;
	while (*s)
		if (!isdigit(*s++))
			return false;
	return true;
	}

unsigned StrToUint(const char *s, bool StarIsUnitMax)
	{
	if (StarIsUnitMax && s[0] == '*' && s[1] == 0)
		return UINT_MAX;
	if (!IsUintStr(s))
		Die("Invalid integer '%s'", s);
	unsigned n = 0;
	while (char c = *s++)
		{
		if (!isdigit(c))
			return n;
		n = n*10 + (c - '0');
		}
	return n;
	}

int StrToInt(const char *s)
	{
	int i = atoi(s);
	return i;
	}

int StrToInt(const string &s)
	{
	return StrToInt(s.c_str());
	}

uint64 StrToUint64(const char *s)
	{
	if (!IsUintStr(s))
		Die("Invalid integer '%s'", s);
	uint64 n = 0;
	while (char c = *s++)
		{
		if (!isdigit(c))
			return n;
		n = n*10 + (c - '0');
		}
	return n;
	}

uint64 StrToUint64(const string &s)
	{
	return StrToUint64(s.c_str());
	}

unsigned StrToUint(const string &s, bool StarIsUnitMax)
	{
	return StrToUint(s.c_str(), StarIsUnitMax);
	}

const char *IntToStr2(uint64 i)
	{
	static char *TmpStr = 0;
	if (TmpStr == 0)
		TmpStr = (char *) malloc(64);
	if (i < 9999)
		sprintf(TmpStr, "%u", (unsigned) i);
	else if (i < UINT_MAX)
		sprintf(TmpStr, "%u (%s)", (unsigned) i, IntToStr(i));
	else
		return IntToStr(i);
	return TmpStr;
	}

const char *PctToStr(double Pct)
	{
	if (Pct == 0.0)
		sprintf(g_TmpStr, "0%%");
	else if (Pct >= 0.1)
		sprintf(g_TmpStr, "%.1f%%", Pct);
	else
		sprintf(g_TmpStr, "%.3f%%", Pct);
	return g_TmpStr;
	}

const char *IntToStrCommas(uint64 i)
	{
	char Tmp[64];
	sprintf(Tmp, "%" PRIu64, i);
	unsigned n = ustrlen(Tmp);
	char Tmp2[64];
	unsigned k = 0;
	for (unsigned i = 0; i < n; ++i)
		{
		char c = Tmp[n-i-1];
		if (i > 0 && i%3 == 0)
			Tmp2[k++] = ',';
		Tmp2[k++] = c;
		}
	for (unsigned i = 0; i < k; ++i)
		g_TmpStr[k-i-1] = Tmp2[i];
	g_TmpStr[k] = 0;
	return g_TmpStr;
	}

const char *IntToStr(uint64 i)
	{
	double d = (double) i;
	if (i < 10000)
		sprintf(g_TmpStr, "%u", (unsigned) i);
	else if (i < 1e6)
		sprintf(g_TmpStr, "%.1fk", d/1e3);
	else if (i < 100e6)
		sprintf(g_TmpStr, "%.1fM", d/1e6);
	else if (i < 1e9)
		sprintf(g_TmpStr, "%.0fM", d/1e6);
	else if (i < 10e9)
		sprintf(g_TmpStr, "%.1fG", d/1e9);
	else if (i < 100e9)
		sprintf(g_TmpStr, "%.0fG", d/1e9);
	else
		sprintf(g_TmpStr, "%.3g", d);
	return g_TmpStr;
	}

const char *Int64ToStr(uint64 i)
	{
	double d = (double) i;
	if (i < 10000)
		sprintf(g_TmpStr, "%u", (unsigned) i);
	else if (i < 1e6)
		sprintf(g_TmpStr, "%.1fk", d/1e3);
	else if (i < 10e6)
		sprintf(g_TmpStr, "%.1fM", d/1e6);
	else if (i < 1e9)
		sprintf(g_TmpStr, "%.0fM", d/1e6);
	else if (i < 10e9)
		sprintf(g_TmpStr, "%.1fG", d/1e9);
	else if (i < 100e9)
		sprintf(g_TmpStr, "%.0fG", d/1e9);
	else
		sprintf(g_TmpStr, "%.3g", d);
	return g_TmpStr;
	}

const char *FloatToStr(double d)
	{
	double a = fabs(d);
	if (a < 0.01)
		sprintf(g_TmpStr, "%.3g", a);
	else if (a >= 0.01 && a < 1)
		sprintf(g_TmpStr, "%.3f", a);
	else if (a <= 10 && a >= 1)
		{
		double intpart;
		if (modf(a, &intpart) < 0.05)
			sprintf(g_TmpStr, "%.0f", d);
		else
			sprintf(g_TmpStr, "%.1f", d);
		}
	else if (a > 10 && a < 10000)
		sprintf(g_TmpStr, "%.1f", d);
	else if (a < 1e6)
		sprintf(g_TmpStr, "%.1fk", d/1e3);
	else if (a < 10e6)
		sprintf(g_TmpStr, "%.1fM", d/1e6);
	else if (a < 1e9)
		sprintf(g_TmpStr, "%.1fM", d/1e6);
	else if (a < 999e9)
		sprintf(g_TmpStr, "%.1fG", d/1e9);
	else if (a < 999e12)
		sprintf(g_TmpStr, "%.1fT", d/1e9);
	else
		sprintf(g_TmpStr, "%.3g", d);
	return g_TmpStr;
	}

const char *FloatToStr(uint64 u)
	{
	return FloatToStr(double(u));
	}

const char *IntFloatToStr(double d)
	{
	double a = fabs(d);
	if (a < 1.0)
		sprintf(g_TmpStr, "%.3g", a);
	else if (a <= 10)
		sprintf(g_TmpStr, "%.0f", d);
	else if (a > 10 && a < 10000)
		sprintf(g_TmpStr, "%.0f", d);
	else if (a < 1e6)
		sprintf(g_TmpStr, "%.1fk", d/1e3);
	else if (a < 10e6)
		sprintf(g_TmpStr, "%.1fM", d/1e6);
	else if (a < 1e9)
		sprintf(g_TmpStr, "%.1fM", d/1e6);
	else if (a < 10e9)
		sprintf(g_TmpStr, "%.1fG", d/1e9);
	else if (a < 100e9)
		sprintf(g_TmpStr, "%.1fG", d/1e9);
	else
		sprintf(g_TmpStr, "%.3g", d);
	return g_TmpStr;
	}

//static string g_CurrentProgressLine;
//static string g_ProgressDesc;
//static unsigned g_ProgressIndex;
//static unsigned g_ProgressCount;
//
//static unsigned g_CurrProgressLineLength;
//static unsigned g_LastProgressLineLength;
//static unsigned g_CountsInterval;
//static unsigned g_StepCalls;
//static time_t g_TimeLastOutputStep;
//
//static string &GetProgressPrefixStr(string &s)
//	{
//	double Bytes = GetMemUseBytes();
//	unsigned Secs = GetElapsedSecs();
//	s = string(SecsToHHMMSS(Secs));
//	if (Bytes > 0)
//		{
//		s.push_back(' ');
//		char Str[32];
//		sprintf(Str, "%-6s", MemBytesToStr(Bytes));
//		s += string(Str);
//		}
//	s.push_back(' ');
//	return s;
//	}

const char *GetElapsedTimeStr(string &s)
	{
	unsigned Secs = GetElapsedSecs();
	s = string(SecsToHHMMSS(Secs));
	return s.c_str();
	}

const char *GetMaxRAMStr(string &s)
	{
	char Str[32];
	sprintf(Str, "%5s", MemBytesToStr(g_PeakMemUseBytes));
	s = string(Str);
	return s.c_str();
	}

//static bool g_ProgressPrefixOn = true;
//
//bool ProgressPrefix(bool On)
//	{
//	bool OldValue = g_ProgressPrefixOn;
//	g_ProgressPrefixOn = On;
//	return OldValue;
//	}
//
//void Progress(const char *Format, ...)
//	{
//	string Str;
//	va_list ArgList;
//	va_start(ArgList, Format);
//	myvstrprintf(Str, Format, ArgList);
//	va_end(ArgList);
//
//	Log("%s", Str.c_str());
//	bool SavedPrefix = g_ProgressPrefixOn;
//	g_ProgressPrefixOn = false;
//	Progress("%s", Str.c_str());
//	g_ProgressPrefixOn = SavedPrefix;
//	}
//
//void ProgressLogPrefix(const char *Format, ...)
//	{
//	string Str;
//	va_list ArgList;
//	va_start(ArgList, Format);
//	myvstrprintf(Str, Format, ArgList);
//	va_end(ArgList);
//
//	Log("%s\n", Str.c_str());
//	Progress("%s\n", Str.c_str());
//	}

void Pr(FILE *f, const char *Format, ...)
	{
	if (f == 0)
		return;

	va_list args;
	va_start(args, Format);
	vfprintf(f, Format, args);
	va_end(args);
	}

//void Progress(const char *Format, ...)
//	{
//	if (opt(quiet))
//		return;
//
//	string Str;
//	va_list ArgList;
//	va_start(ArgList, Format);
//	myvstrprintf(Str, Format, ArgList);
//	va_end(ArgList);
//
//#if	0
//	Log("Progress(");
//	for (unsigned i = 0; i < Str.size(); ++i)
//		{
//		char c = Str[i];
//		if (c == '\r')
//			Log("\\r");
//		else if (c == '\n')
//			Log("\\n");
//		else
//			Log("%c", c);
//		}
//	Log(")\n");
//#endif //0
//
//	for (unsigned i = 0; i < Str.size(); ++i)
//		{
//		if (g_ProgressPrefixOn && g_CurrProgressLineLength == 0)
//			{
//			string s;
//			GetProgressPrefixStr(s);
//			for (unsigned j = 0; j < s.size(); ++j)
//				{
//				fputc(s[j], stderr);
//				++g_CurrProgressLineLength;
//				}
//			}
//
//		char c = Str[i];
//		if (c == '\n' || c == '\r')
//			{
//			for (unsigned j = g_CurrProgressLineLength; j < g_LastProgressLineLength; ++j)
//				fputc(' ', stderr);
//			if (c == '\n')
//				g_LastProgressLineLength = 0;
//			else
//				g_LastProgressLineLength = g_CurrProgressLineLength;
//			g_CurrProgressLineLength = 0;
//			fputc(c, stderr);
//			}
//		else
//			{
//			fputc(c, stderr);
//			++g_CurrProgressLineLength;
//			}
//		}
//	}

void LogProgramInfoAndCmdLine()
	{
	PrintProgramInfo(g_fLog);
	PrintCmdLine(g_fLog);
#ifdef	_MSC_VER
	const char *e = getenv("CYGTZ");
	if (e != 0 && strcmp(e, "YES") == 0)
		putenv("TZ=");
#endif
	time_t Now = time(0);
	struct tm *t = localtime(&Now);
	const char *s = asctime(t);
	Log("Started %s", s); // there is a newline in s
	}

void LogElapsedTimeAndRAM()
	{
	time_t Now = time(0);
	struct tm *t = localtime(&Now);
	const char *s = asctime(t);
	unsigned Secs = GetElapsedSecs();

	Log("\n");
	Log("Finished %s", s); // there is a newline in s
	Log("Elapsed time %s\n", SecsToHHMMSS((int) Secs));
	Log("Max memory %s\n", MemBytesToStr(g_PeakMemUseBytes));
#if	WIN32 && DEBUG
// Skip exit(), which can be very slow in DEBUG build
// VERY DANGEROUS practice, because it skips global destructors.
// But if you know the rules, you can break 'em, right?
	ExitProcess(0);
#endif
	}

const char *PctStr(double x, double y)
	{
	if (y == 0)
		{
		if (x == 0)
			return "100%";
		else
			return "inf%";
		}
	static char Str[16];
	double p = x*100.0/y;
	sprintf(Str, "%5.1f%%", p);
	return Str;
	}

#if TIMING
static time_t g_LastLogTimerSecs; 
#endif

static unsigned GetStructPack()
	{
	struct
		{
		char a;
		char b;
		} x;

	return (unsigned) (&x.b - &x.a);
	}

void CompilerInfo()
	{
	printf("%u bits\n", BITS);

#ifdef __GNUC__
	printf("__GNUC__\n");
#endif

#ifdef __APPLE__
	printf("__APPLE__\n");
#endif

#ifdef _MSC_VER
	printf("_MSC_VER %d\n", _MSC_VER);
#endif

#define x(t)	printf("sizeof(" #t ") = %d\n", (int) sizeof(t));
	x(int)
	x(long)
	x(float)
	x(double)
	x(void *)
	x(off_t)
	x(size_t)
#undef x

	printf("pack(%u)\n", GetStructPack());

#ifdef _FILE_OFFSET_BITS
    printf("_FILE_OFFSET_BITS = %d\n", _FILE_OFFSET_BITS);
#else
    printf("_FILE_OFFSET_BITS not defined\n");
#endif

	exit(0);
	}

bool StartsWith(const char *S, const char *T)
	{
	for (;;)
		{
		char t = *T++;
		if (t == 0)
			return true;
		char s = *S++;
		if (s != t)
			return false;
		}
	}

void Reverse(string &s)
	{
	unsigned n = SIZE(s);
	string t;
	for (unsigned i = 0; i < n; ++i)
		t += s[n-i-1];
	s = t;
	}

bool StartsWith(const string &S, const char *T)
	{
	return StartsWith(S.c_str(), T);
	}

bool StartsWith(const string &s, const string &t)
	{
	return StartsWith(s.c_str(), t.c_str());
	}

void ToUpper(const string &s, string &t)
	{
	t.clear();
	const unsigned n = SIZE(s);
	for (unsigned i = 0; i < n; ++i)
		t.push_back(toupper(s[i]));
	}

void ToLower(const string &s, string &t)
	{
	t.clear();
	const unsigned n = SIZE(s);
	for (unsigned i = 0; i < n; ++i)
		t.push_back(tolower(s[i]));
	}

void TruncWhiteSpace(string &Str)
	{
	for (unsigned i = 0; i < SIZE(Str); ++i)
		{
		char c = Str[i];
		if (isspace(c))
			{
			Str.resize(i);
			return;
			}
		}
	}

void StripWhiteSpace(string &Str)
	{
	unsigned n = SIZE(Str);
	unsigned FirstNonWhite = UINT_MAX;
	unsigned LastNonWhite = UINT_MAX;
	for (unsigned i = 0; i < n; ++i)
		{
		char c = Str[i];
		if (!isspace(c))
			{
			if (FirstNonWhite == UINT_MAX)
				FirstNonWhite = i;
			LastNonWhite = i;
			}
		}

	if (FirstNonWhite == UINT_MAX)
		return;

	string t;
	for (unsigned i = FirstNonWhite; i <= LastNonWhite; ++i)
		{
		char c = Str[i];
		t += c;
		}
	Str = t;
	}

void Split(const string &Str, vector<string> &Fields, char Sep)
	{
	Fields.clear();
	const unsigned Length = (unsigned) Str.size();
	string s;
	for (unsigned i = 0; i < Length; ++i)
		{
		char c = Str[i];
		if ((Sep == 0 && isspace(c)) || c == Sep)
			{
			if (!s.empty() || Sep != 0)
				Fields.push_back(s);
			s.clear();
			}
		else
			s.push_back(c);
		}
	if (!s.empty())
		Fields.push_back(s);
	}

void Version(FILE *f)
	{
	if (f == 0)
		return;
	const char *Flags = ""
#if	DEBUG
	"D"
#endif
#if TIMING
	"T"
#endif
	;
	fprintf(f, PROGRAM_NAME " v%s.%s_%s%s", MY_VERSION, SVN_VERSION, GetPlatform(), Flags);
	}

void cmd_version()
	{
	Version(stdout);
	printf("\n");
	exit(0);
	}

void Help()
	{
	PrintProgramInfo(stdout);
	PrintCopyright(stdout);
	exit(0);
	}

void PrintProgramInfo(FILE *f)
	{
	if (f == 0)
		return;

	fprintf(f, "\n");
	Version(f);

	double RAM = GetPhysMemBytes();
	double UsableRAM = GetUsableMemBytes();
	if (RAM > 0)
		{
		if (RAM == UsableRAM)
			fprintf(f, ", %s RAM", MemBytesToStr(RAM));
		else
			{
			fprintf(f, ", %s RAM", MemBytesToStr(UsableRAM));
			fprintf(f, " (%s total)", MemBytesToStr(RAM));
			}
		fprintf(f, ", %u cores\n", GetCPUCoreCount());
		}
	}

void PrintCopyright(FILE *f)
	{
	if (f == 0)
		return;

	fprintf(f, "(C) Copyright 2019 Robert C. Edgar\n");
	fprintf(f, "https://drive5.com/urmap\n");
	fprintf(f, "\n");
	}

void PrintCmdLine(FILE *f)
	{
	if (f == 0)
		return;
	for (unsigned i = 0; i < SIZE(g_Argv); ++i)
		fprintf(f, "%s ", g_Argv[i].c_str());
	fprintf(f, "\n");
	}

void GetCmdLine(string &s)
	{
	s.clear();
	for (unsigned i = 0; i < SIZE(g_Argv); ++i)
		{
		if (i > 0)
			s += " ";
		s += g_Argv[i];
		}
	}

char *mystrsave(const char *s)
	{
	unsigned n = unsigned(strlen(s));
	char *t = myalloc(char, n+1);
	memcpy(t, s, n+1);
	return t;
	}

unsigned myipow(unsigned x, unsigned y)
	{
	unsigned result = 1;
	for (unsigned k = 0; k < y; ++k)
		{
		if (result > UINT_MAX/x)
			Die("myipow(%u, %u), overflow", x, y);
		result *= x;
		}
	return result;
	}

uint64 myipow64(unsigned x, unsigned y)
	{
	uint64 result = 1;
	for (unsigned k = 0; k < y; ++k)
		{
		if (result > uint64(UINT64_MAX)/uint64(x))
			Die("myipow(%u, %u), overflow", x, y);
		result *= x;
		}
	return result;
	}

void LogInt(unsigned i, unsigned w)
    {
    if (w == UINT_MAX)
        {
        if (i < 9999)
            Log("%u", i);
        else
            Log("%u (%s)", i, IntToStr(i));
        }
    else
        {
        if (i < 9999)
            Log("%*u", w, i);
        else
            Log("%*u (%s)", w, i, IntToStr(i));
        }
    }

void Logu(unsigned u, unsigned w, unsigned prefixspaces)
	{
	for (unsigned i = 0; i < prefixspaces; ++i)
		Log(" ");
	if (u == UINT_MAX)
		Log("%*.*s", w, w, "*");
	else
		Log("%*u", w, u);
	}

void Logf(float x, unsigned w, unsigned prefixspaces)
	{
	for (unsigned i = 0; i < prefixspaces; ++i)
		Log(" ");
	if (x == FLT_MAX)
		Log("%*.*s", w, w, "*");
	else
		Log("%*.2f", w, x);
	}

static uint32 g_SLCG_state = 1;

// Simple Linear Congruential Generator
// Bad properties; used just to initialize the better generator.
// Numerical values used by Microsoft C, according to wikipedia:
// http://en.wikipedia.org/wiki/Linear_congruential_generator
static uint32 g_SLCG_a = 214013;
static uint32 g_SLCG_c = 2531011;

static uint32 SLCG_rand()
	{
	g_SLCG_state = g_SLCG_state*g_SLCG_a + g_SLCG_c;
	return g_SLCG_state;
	}

static void SLCG_srand(uint32 Seed)
	{
	g_SLCG_state = Seed;
	for (int i = 0; i < 10; ++i)
		SLCG_rand();
	}

/***
A multiply-with-carry random number generator, see:
http://en.wikipedia.org/wiki/Multiply-with-carry

The particular multipliers used here were found on
the web where they are attributed to George Marsaglia.
***/

static bool g_InitRandDone = false;
static uint32 g_X[5];

static void InitRand()
	{
	if (g_InitRandDone)
		return;
// Do this first to avoid recursion
	g_InitRandDone = true;

	unsigned Seed;
	if (optset_randseed)
		Seed = opt(randseed);
	else
		Seed = (unsigned) (time(0)*getpid());
	ResetRand(Seed);
	}

static void IncrementRand()
	{
	uint64 Sum = 2111111111*(uint64) g_X[3] + 1492*(uint64) g_X[2] +
	  1776*(uint64) g_X[1] + 5115*(uint64) g_X[0] + g_X[4];
	g_X[3] = g_X[2];
	g_X[2] = g_X[1];
	g_X[1] = g_X[0];
	g_X[4] = (uint32) (Sum >> 32);
	g_X[0] = (uint32) Sum;
	}

uint32 RandInt32()
	{
	InitRand();
	IncrementRand();
	return g_X[0];
	}

unsigned randu32()
	{
	return (unsigned) RandInt32();
	}

uint64 randu64()
	{
	union
		{
		struct
			{
			uint32 u32[2];
			};
		uint64 u64;
		} x;
	x.u32[0] = randu32();
	x.u32[1] = randu32();
	return x.u64;
	}

void ResetRand(unsigned Seed)
	{
	SLCG_srand(Seed);

	for (unsigned i = 0; i < 5; i++)
		g_X[i] = SLCG_rand();

	for (unsigned i = 0; i < 100; i++)
		IncrementRand();
	}

unsigned GetCPUCoreCount()
	{
#if (defined(_MSC_VER) || defined(__MINGW32__))
	SYSTEM_INFO SI;
	GetSystemInfo(&SI);
	unsigned n = SI.dwNumberOfProcessors;
	if (n == 0 || n > 64)
		return 1;
	return n;
#else
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	if (n <= 0)
		return 1;
	return (unsigned) n;
#endif
	}

unsigned GetThreadIndex()
	{
	return omp_get_thread_num();
	}

// MUST COME AT END BECAUSE OF #undefs
#undef myalloc
#undef myfree

#if	RCE_MALLOC
#undef mymalloc
#undef myfree
#undef myfree2

static unsigned g_NewCalls;
static unsigned g_FreeCalls;
static double g_InitialMemUseBytes;
static double g_TotalAllocBytes;
static double g_TotalFreeBytes;
static double g_NetBytes;
static double g_MaxNetBytes;

void LogAllocStats()
	{
	Log("\n");
	Log("       Allocs  %u\n", g_NewCalls);
	Log("        Frees  %u\n", g_FreeCalls);
	Log("Initial alloc  %s\n", MemBytesToStr(g_InitialMemUseBytes));
	Log("  Total alloc  %s\n", MemBytesToStr(g_TotalAllocBytes));
	Log("   Total free  %s\n", MemBytesToStr(g_TotalFreeBytes));
	Log("    Net bytes  %s\n", MemBytesToStr(g_NetBytes));
	Log("Max net bytes  %s\n", MemBytesToStr(g_MaxNetBytes));
	Log("   Peak total  %s\n", MemBytesToStr(g_MaxNetBytes + g_InitialMemUseBytes));
	}

void *mymalloc(unsigned n, unsigned bytes, const char *FileName, int Line)
	{
//	void *rce_malloc(unsigned bytes, const char *FileName, int Line);
	return rce_malloc(n, bytes, FileName, Line);
	}

void myfree(void *p, const char *FileName, int Line)
	{
//	void rce_free(void *p, const char *FileName, int Line);
	rce_free(p, FileName, Line);
	}

void myfree2(void *p, unsigned bytes, const char *FileName, int Line)
	{
//	void rce_free(void *p, const char *FileName, int Line);
	rce_free(p, FileName, Line);
	}

#else // RCE_MALLOC

#if	ALLOC_TOTALS
void LogAllocSummary()
	{
	extern unsigned g_AllocCount;
	extern unsigned g_FreeCount;
	extern uint64 g_AllocTotal;
	extern uint64 g_FreeTotal;

	double RAM = GetMemUseBytes();
	Log("RAM %s", MemBytesToStr(RAM));
	Log(", malloc %s", MemBytesToStr(g_AllocTotal));
	Log(", free %s",  MemBytesToStr(g_FreeTotal));
	Log(", net %s\n", MemBytesToStr(g_AllocTotal - g_FreeTotal));
	}
#endif

void *mymalloc64(uint64 BytesPerObject, uint64 N)
	{
	uint64 Bytes64 = BytesPerObject*N;

#if USE_DBG_MALLOC
	void *p = _malloc_dbg(Bytes64, _NORMAL_BLOCK, __FILE__, __LINE__);
#else
	void *p = malloc(Bytes64);
#endif
	if (p == 0)
		Die("myalloc64(%" PRIu64 ", %" PRIu64 ") failed", BytesPerObject, N);
	return p;
	}

void *mymalloc(unsigned n, unsigned bytes)
	{
	++g_AllocCount;
	uint64 Bytes64 = uint64(n)*uint64(bytes);

	if (Bytes64 > uint64(UINT_MAX))
		Die("%s(%u): mymalloc(%u, %u) overflow", g_AllocFile, g_AllocLine, n, bytes);

#if	ALLOC_TOTALS
	g_AllocTotal += Bytes64;
	Bytes64 += 4;
#endif
	uint32 Bytes32 = uint32(Bytes64);
#if USE_DBG_MALLOC
	void *p = _malloc_dbg(Bytes32, _NORMAL_BLOCK, __FILE__, __LINE__);
#else
	void *p = malloc(Bytes32);
#endif
	if (0 == p)
		{
		double b = GetMemUseBytes();
		double Total = b + double(Bytes32);

#if	BITS==32
		if (Total > 2e9)
			{
			Log("\n%s(%u): Out of memory, mymalloc(%u, %u), curr %.3g bytes, total %.3g (%s)\n",
			  g_AllocFile, g_AllocLine, n, bytes, b, Total, MemBytesToStr(Total));
			Die("Memory limit of 32-bit process exceeded, 64-bit build required");
			}
#endif

		fprintf(stderr, "\n%s(%u): Out of memory mymalloc(%u), curr %.3g bytes",
		  g_AllocFile, g_AllocLine, (unsigned) bytes, b);
#if DEBUG && defined(_MSC_VER)
		asserta(_CrtCheckMemory());
#endif
		Die("%s(%u): Out of memory, mymalloc(%u, %u), curr %.3g bytes, total %.3g (%s)\n",
		  g_AllocFile, g_AllocLine, n, bytes, b, Total, MemBytesToStr(Total));
		}
#if	ALLOC_TOTALS
	*((uint32 *) p) = Bytes32;
	return (void *) ((byte *) p + 4);
#else
	return p;
#endif
	}

void myfree(void *p)
	{
	if (p == 0)
		return;
	++g_FreeCount;
#if	ALLOC_TOTALS
	uint32 *pi = (uint32 *) p;
	uint32 Bytes32 = *(pi - 1);
	g_FreeTotal += Bytes32;
	free((void *) (pi - 1));
#else
#if USE_DBG_MALLOC
	_free_dbg(p, _NORMAL_BLOCK);
#else
	free(p);
#endif
#endif
	}

#endif // RCE_MALLOC
