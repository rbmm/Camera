#include "stdafx.h"

_NT_BEGIN

#include "file.h"

NTSTATUS ReadFromFile(_In_ PCWSTR lpFileName, 
					  _Out_ PBYTE* ppb, 
					  _Out_ ULONG* pcb, 
					  _In_ ULONG MinSize /*= 0*/, 
					  _In_ ULONG MaxSize /*= MAXLONG*/)
{
	UNICODE_STRING ObjectName;

	NTSTATUS status = RtlDosPathNameToNtPathName_U_WithStatus(lpFileName, &ObjectName, 0, 0);

	if (0 <= status)
	{
		HANDLE hFile;
		IO_STATUS_BLOCK iosb;
		OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };

		status = NtOpenFile(&hFile, FILE_GENERIC_READ, &oa, &iosb, FILE_SHARE_READ, 
			FILE_SYNCHRONOUS_IO_NONALERT|FILE_NON_DIRECTORY_FILE);

		RtlFreeUnicodeString(&ObjectName);

		if (0 <= status)
		{
			FILE_STANDARD_INFORMATION fsi;
			if (0 <= (status = NtQueryInformationFile(hFile, &iosb, &fsi, sizeof(fsi), FileStandardInformation)))
			{
				if (MinSize < fsi.EndOfFile.QuadPart && fsi.EndOfFile.QuadPart <= MaxSize)
				{
					if (PBYTE pb = new UCHAR[fsi.EndOfFile.LowPart])
					{
						if (0 > (status = NtReadFile(hFile, 0, 0, 0, &iosb, pb, fsi.EndOfFile.LowPart, 0, 0)))
						{
							delete [] pb;
						}
						else
						{
							*ppb = pb;
							*pcb = (ULONG)iosb.Information;
						}
					}
					else
					{
						status = STATUS_NO_MEMORY;
					}
				}
				else
				{
					status = STATUS_FILE_TOO_LARGE;
				}
			}
			NtClose(hFile);
		}
	}

	return status;
}

NTSTATUS SaveToFile(_In_ PCWSTR lpFileName, _In_ const void* lpBuffer, _In_ ULONG nNumberOfBytesToWrite)
{
	UNICODE_STRING ObjectName;

	NTSTATUS status = RtlDosPathNameToNtPathName_U_WithStatus(lpFileName, &ObjectName, 0, 0);

	if (0 <= status)
	{
		HANDLE hFile;
		IO_STATUS_BLOCK iosb;
		OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };

		LARGE_INTEGER AllocationSize = { nNumberOfBytesToWrite };

		status = NtCreateFile(&hFile, FILE_APPEND_DATA|SYNCHRONIZE, &oa, &iosb, &AllocationSize,
			0, 0, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT|FILE_NON_DIRECTORY_FILE, 0, 0);

		RtlFreeUnicodeString(&ObjectName);

		if (0 <= status)
		{
			status = NtWriteFile(hFile, 0, 0, 0, &iosb, const_cast<void*>(lpBuffer), nNumberOfBytesToWrite, 0, 0);
			NtClose(hFile);
		}
	}

	return status;
}

_NT_END