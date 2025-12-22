extern @?FastReferenceDll:proc
extern ?DereferenceDll@NT@@YAXXZ:proc

; __int64 __cdecl NT::ZSubClass::WrapperWindowProc(struct HWND__ *,unsigned int,unsigned __int64,__int64)
extern ?WrapperWindowProc@ZSubClass@NT@@AEAA_JPEAUHWND__@@I_K_J@Z : PROC

_TEXT segment 'CODE'

?SubClassProc@ZSubClass@NT@@CA_JPEAUHWND__@@I_K_J11@Z proc
	call @?FastReferenceDll 
	sub rsp,38h
	mov [rsp+20h],r9
	mov r9,r8
	mov r8,rdx
	mov rdx,rcx
	mov rcx,[rsp + 68h]
	call ?WrapperWindowProc@ZSubClass@NT@@AEAA_JPEAUHWND__@@I_K_J@Z
	add rsp,38h
	jmp ?DereferenceDll@NT@@YAXXZ
?SubClassProc@ZSubClass@NT@@CA_JPEAUHWND__@@I_K_J11@Z endp

_TEXT ENDS
END
