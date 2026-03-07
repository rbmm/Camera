#pragma once

NTSTATUS ReadFromFile(_In_ PCWSTR lpFileName, 
					  _Out_ PBYTE* ppb, 
					  _Out_ ULONG* pcb, 
					  _In_ ULONG MinSize = 0, 
					  _In_ ULONG MaxSize = MAXLONG);

NTSTATUS SaveToFile(_In_ PCWSTR lpFileName, _In_ const void* lpBuffer, _In_ ULONG nNumberOfBytesToWrite);