#include "queue.h"

void mmap_file(wchar_t path[], unsigned int size, volatile LPVOID *buffer, HANDLE *mapping_handle, HANDLE *file_handle)
{
	*file_handle = CreateFile(
		path,
		GENERIC_WRITE | GENERIC_READ,
		/* share read is important!
		* otherwise it will map to separate mem region
		*/
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY,
		NULL);
	*mapping_handle = CreateFileMapping(
		*file_handle,
		NULL,
		PAGE_READWRITE,
		0,
		size,
		NULL);
	*buffer = (LPVOID)MapViewOfFile(*mapping_handle,
		FILE_MAP_ALL_ACCESS,
		0,
		0,
		size);
}

void delete_files_in_folder(char* home)
{
	WIN32_FIND_DATA info;
	HANDLE hp; 
	char path_mb_str[STR_BUFFER_LEN];
	strcpy_s(path_mb_str, STR_BUFFER_LEN, home);
	strcat_s(path_mb_str, STR_BUFFER_LEN, "*.*");
	wchar_t path_wide_str[STR_BUFFER_LEN];
	mb_to_wide_str(path_mb_str, path_wide_str);
	hp = FindFirstFile(path_wide_str, &info);
	do {
		strcpy_s(path_mb_str, STR_BUFFER_LEN, home);
		mb_to_wide_str(path_mb_str, path_wide_str);
		wcsncat_s(
			path_wide_str,
			STR_BUFFER_LEN,
			info.cFileName,
			wcslen(info.cFileName));
		DeleteFile(path_wide_str);
	} while (FindNextFile(hp, &info));
	FindClose(hp);
}

BOOL directory_exists(LPCTSTR path)
{
	DWORD attr = GetFileAttributes(path);
	return attr != INVALID_FILE_ATTRIBUTES &&
		(attr & FILE_ATTRIBUTE_DIRECTORY);
}
BOOL file_exists(LPCTSTR path) 
{
	DWORD attr = GetFileAttributes(path);
	if (0xFFFFFFFF == attr) 
		return false; 
	return true; 
}
void panic(char* msg) {
	printf("PANIC: %s", msg);
	exit(1);
}

void mb_to_wide_str(char* mb_str, wchar_t* wide_str) {
	MultiByteToWideChar(CP_ACP, 0, mb_str, -1, wide_str, STR_BUFFER_LEN);
}

int16_t pin_thread_to_core(int16_t core_id)
{
	DWORD_PTR old_mask = SetThreadAffinityMask(GetCurrentThread(), 1i64 << core_id);
	if (NULL == old_mask)
		return ERR_PIN_TO_CORE_FAILED;
	else
		return ERR_OK;
}

int16_t is_queue_interrupted(queue_handle_t h)
{
	volatile queue_t *q = _queues[h];
	volatile queue_buffer_t *qb = q->buffer;
	if (qb->interrupted)
		return ERR_QUEUE_INTERRUPTED;
	else 
		return 0;
}

uint64_t queue_length(queue_handle_t h)
{
	return _queues[h]->length;
}

void lock(volatile DWORD *lock)
{
	while (1) {
		if (0 == InterlockedCompareExchange(lock, 1, 0))
			break;
		YieldProcessor();
	}
}

void unlock(volatile DWORD *lock)
{
	*lock = 0;
	MemoryBarrier();
}

void _lock_queues()
{
	while (1) {
		if (0 == InterlockedCompareExchange(&_lock_open_close, 1, 0))
			break;
		YieldProcessor();
	}
}

void _unlock_queues()
{ 
	_lock_open_close = 0; 
	MemoryBarrier(); 
}
