#include "efi.h"
#include "font.h"

// --- Configuration ---
#define BG_COLOR        0xFF0A0A0A  
#define FG_COLOR        0xFFAAAAAA  
#define PROMPT_USER     0xFF00FF00  
#define PROMPT_HOST     0xFF5F5FFF  
#define ERR_COLOR       0xFFFF5555  
#define DIR_COLOR       0xFF5F5FFF  
#define CMD_COLOR       0xFFFFFFFF

// --- Global State ---
EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = 0;
EFI_FILE_PROTOCOL *RootFS = 0;
UINT32 *Framebuffer = 0;
UINT32 ScreenWidth = 0;
UINT32 ScreenHeight = 0;
UINT32 Pitch = 0;
UINTN CursorX = 10, CursorY = 40; 

CHAR16 CurrentPath[256] = {L'\\', 0}; 

// --- String Helpers ---
UINTN StrLen(const CHAR16 *str) {
    UINTN len = 0;
    while (str[len]) len++;
    return len;
}

BOOLEAN StrCmp(const CHAR16 *a, const CHAR16 *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

void StrCpy(CHAR16 *dest, const CHAR16 *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

void StrCat(CHAR16 *dest, const CHAR16 *src) {
    while (*dest) dest++;
    while (*src) *dest++ = *src++;
    *dest = 0;
}

void ToChar(CHAR16 *in, char *out) {
    while (*in) *out++ = (char)*in++;
    *out = 0;
}

void *memset(void *s, int c, UINTN n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

// --- Graphics Engine ---
void PutPixel(UINTN x, UINTN y, UINT32 color) {
    if (x < ScreenWidth && y < ScreenHeight) Framebuffer[y * Pitch + x] = color;
}

void DrawRect(UINTN x, UINTN y, UINTN w, UINTN h, UINT32 color) {
    for (UINTN row = 0; row < h; row++)
        for (UINTN col = 0; col < w; col++) PutPixel(x + col, y + row, color);
}

void DrawChar(UINTN x, UINTN y, char c, UINT32 color) {
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = font8x8_basic[c - 32];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if ((glyph[row] >> (7 - col)) & 1) {
                // Scale 2x
                PutPixel(x + col * 2, y + row * 2, color);
                PutPixel(x + col * 2 + 1, y + row * 2, color);
                PutPixel(x + col * 2, y + row * 2 + 1, color);
                PutPixel(x + col * 2 + 1, y + row * 2 + 1, color);
            }
        }
    }
}

void Scroll() {
    DrawRect(0, 30, ScreenWidth, ScreenHeight - 30, BG_COLOR);
    CursorX = 10; CursorY = 40;
}

void kprint(const char *str, UINT32 color) {
    while (*str) {
        if (*str == '\n') { CursorX = 10; CursorY += 20; }
        else if (*str == '\r') { CursorX = 10; }
        else if (*str == '\b') { 
            if (CursorX > 10) { CursorX -= 16; DrawRect(CursorX, CursorY, 16, 16, BG_COLOR); }
        } else {
            DrawChar(CursorX, CursorY, *str, color);
            CursorX += 16;
            if (CursorX >= ScreenWidth - 10) { CursorX = 10; CursorY += 20; }
        }
        if (CursorY >= ScreenHeight - 20) Scroll();
        str++;
    }
}

void kprint_int(UINTN num) {
    char buf[32]; int i = 0;
    if (num == 0) { kprint("0", FG_COLOR); return; }
    while (num > 0) { buf[i++] = (num % 10) + '0'; num /= 10; }
    for (int j = 0; j < i / 2; j++) { char t = buf[j]; buf[j] = buf[i - 1 - j]; buf[i - 1 - j] = t; }
    buf[i] = 0; kprint(buf, FG_COLOR);
}

void kprint16(CHAR16 *str, UINT32 color) {
    char buf[256];
    ToChar(str, buf);
    kprint(buf, color);
}

// --- Path Logic ---
int GetAbsolutePath(CHAR16 *filename, CHAR16 *outBuf) {
    StrCpy(outBuf, CurrentPath);
    StrCat(outBuf, filename);
    return 1;
}

EFI_STATUS OpenFileRel(CHAR16 *path, EFI_FILE_PROTOCOL **Handle, UINT64 Mode, UINT64 Attr) {
    CHAR16 AbsPath[256];
    GetAbsolutePath(path, AbsPath);
    return RootFS->Open(RootFS, Handle, AbsPath, Mode, Attr);
}

// --- Commands ---

void CmdLS() {
    if (!RootFS) return;
    EFI_FILE_PROTOCOL *DirHandle;
    EFI_STATUS Status = RootFS->Open(RootFS, &DirHandle, CurrentPath, EFI_FILE_MODE_READ, 0);
    if (Status != EFI_SUCCESS) { kprint("Error reading directory.\n", ERR_COLOR); return; }

    DirHandle->SetPosition(DirHandle, 0);
    
    char Buffer[512]; 
    EFI_FILE_INFO *Info = (EFI_FILE_INFO *)Buffer;
    UINTN Size = sizeof(Buffer);

    while (1) {
        Size = sizeof(Buffer);
        Status = DirHandle->Read(DirHandle, &Size, Info);
        if (Status != EFI_SUCCESS || Size == 0) break;

        char Name[64]; ToChar(Info->FileName, Name);
        if (StrCmp(Info->FileName, (CHAR16*)L".")) continue;

        if (Info->Attribute & EFI_FILE_DIRECTORY) {
            kprint(" [DIR] ", DIR_COLOR); kprint(Name, DIR_COLOR);
        } else {
            kprint("       ", FG_COLOR); kprint(Name, FG_COLOR);
        }
        kprint("\n", FG_COLOR);
    }
    DirHandle->Close(DirHandle);
}

void CmdCD(CHAR16 *target) {
    if (StrCmp(target, (CHAR16*)L"..")) {
        UINTN len = StrLen(CurrentPath);
        if (len <= 1) return; 
        if (CurrentPath[len-1] == L'\\') len--;
        while (len > 0 && CurrentPath[len-1] != L'\\') { len--; }
        CurrentPath[len] = 0; 
        if (len == 0) { CurrentPath[0] = L'\\'; CurrentPath[1] = 0; }
        return;
    }

    EFI_FILE_PROTOCOL *Handle;
    EFI_STATUS Status = OpenFileRel(target, &Handle, EFI_FILE_MODE_READ, 0);
    if (Status != EFI_SUCCESS) { kprint("Directory not found.\n", ERR_COLOR); return; }
    
    StrCat(CurrentPath, target);
    StrCat(CurrentPath, (CHAR16*)L"\\");
    Handle->Close(Handle);
}

void CmdPWD() { kprint16(CurrentPath, FG_COLOR); kprint("\n", FG_COLOR); }

void CmdMkDir(CHAR16 *name) {
    EFI_FILE_PROTOCOL *NewHandle;
    EFI_STATUS Status = OpenFileRel(name, &NewHandle, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
    if (Status == EFI_SUCCESS) { kprint("Created directory.\n", FG_COLOR); NewHandle->Close(NewHandle); } 
    else { kprint("Failed.\n", ERR_COLOR); }
}

void CmdTouch(CHAR16 *name) {
    EFI_FILE_PROTOCOL *NewHandle;
    // Cast strict UINT64 for mode to avoid warnings
    EFI_STATUS Status = OpenFileRel(name, &NewHandle, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (Status == EFI_SUCCESS) { kprint("File created.\n", FG_COLOR); NewHandle->Close(NewHandle); } 
    else { kprint("Failed.\n", ERR_COLOR); }
}

void CmdRm(CHAR16 *name) {
    EFI_FILE_PROTOCOL *Handle;
    EFI_STATUS Status = OpenFileRel(name, &Handle, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (Status != EFI_SUCCESS) { kprint("Not found.\n", ERR_COLOR); return; }
    Handle->Delete(Handle);
    kprint("Deleted.\n", FG_COLOR);
}

void CmdCat(CHAR16 *name) {
    EFI_FILE_PROTOCOL *Handle;
    if (OpenFileRel(name, &Handle, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS) { kprint("File not found.\n", ERR_COLOR); return; }
    char *Buf = (char*)0x00400000;
    UINTN Size = 10000;
    Handle->Read(Handle, &Size, Buf);
    Buf[Size] = 0;
    kprint(Buf, FG_COLOR); kprint("\n", FG_COLOR);
    Handle->Close(Handle);
}

void CmdEdit(CHAR16 *name, EFI_SYSTEM_TABLE *ST) {
    DrawRect(0, 30, ScreenWidth, ScreenHeight - 30, BG_COLOR); 
    CursorX = 10; CursorY = 40;
    kprint("--- VIBEOS EDITOR (ESC to Save) ---\n\n", PROMPT_HOST);

    char *EditBuf = (char*)0x00500000;
    UINTN Index = 0;
    memset(EditBuf, 0, 10000);

    EFI_FILE_PROTOCOL *Handle;
    if (OpenFileRel(name, &Handle, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS) {
        UINTN Size = 9990;
        Handle->Read(Handle, &Size, EditBuf);
        Handle->Close(Handle);
        Index = Size;
        kprint(EditBuf, FG_COLOR);
    }

    while (1) {
        EFI_INPUT_KEY Key;
        if (ST->ConIn->ReadKeyStroke(ST->ConIn, &Key) != EFI_SUCCESS) continue;
        if (Key.ScanCode == 0x17) break; 
        if (Key.UnicodeChar == L'\r') { EditBuf[Index++] = '\r'; EditBuf[Index++] = '\n'; kprint("\n", FG_COLOR); }
        else if (Key.UnicodeChar == L'\b') { if (Index > 0) { Index--; kprint("\b", FG_COLOR); } }
        else if (Key.UnicodeChar >= 32 && Key.UnicodeChar < 127) {
            EditBuf[Index++] = (char)Key.UnicodeChar;
            char s[2] = {(char)Key.UnicodeChar, 0};
            kprint(s, FG_COLOR);
        }
    }

    if (OpenFileRel(name, &Handle, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0) == EFI_SUCCESS) {
        Handle->Write(Handle, &Index, EditBuf);
        Handle->Close(Handle);
    }
    Scroll();
    kprint("File saved.\n", FG_COLOR);
}

void CmdNeofetch() {
    kprint("\n", 0);
    kprint("   .---.    ", PROMPT_USER); kprint("root@vibeos\n", PROMPT_USER);
    kprint("  /     \\   ", PROMPT_USER); kprint("-----------\n", FG_COLOR);
    kprint("  |  V  |   ", PROMPT_USER); kprint("OS: VibeOS v5.0\n", FG_COLOR);
    kprint("  \\     /   ", PROMPT_USER); kprint("Filesystem: FAT32\n", FG_COLOR);
    kprint("   '---'    ", PROMPT_USER); kprint("Resolution: ", FG_COLOR); kprint_int(ScreenWidth); kprint("x", FG_COLOR); kprint_int(ScreenHeight); kprint("\n", 0);
    kprint("\n", 0);
}

void CmdHelp() {
    kprint("VibeOS Manual:\n", PROMPT_HOST);
    kprint("  ls          ", CMD_COLOR); kprint("- List files in current directory\n", FG_COLOR);
    kprint("  cd [dir]    ", CMD_COLOR); kprint("- Change directory (use '..' to go back)\n", FG_COLOR);
    kprint("  pwd         ", CMD_COLOR); kprint("- Print working directory\n", FG_COLOR);
    kprint("  mkdir [dir] ", CMD_COLOR); kprint("- Create new directory\n", FG_COLOR);
    kprint("  touch [file]", CMD_COLOR); kprint("- Create new empty file\n", FG_COLOR);
    kprint("  rm [name]   ", CMD_COLOR); kprint("- Remove file or empty directory\n", FG_COLOR);
    kprint("  cat [file]  ", CMD_COLOR); kprint("- Print file content to screen\n", FG_COLOR);
    kprint("  edit [file] ", CMD_COLOR); kprint("- Open visual text editor\n", FG_COLOR);
    kprint("  neofetch    ", CMD_COLOR); kprint("- Show system info\n", FG_COLOR);
    kprint("  clear       ", CMD_COLOR); kprint("- Clear screen\n", FG_COLOR);
    kprint("  reboot      ", CMD_COLOR); kprint("- Restart computer\n", FG_COLOR);
    kprint("  off         ", CMD_COLOR); kprint("- Shutdown computer\n", FG_COLOR);
}

void DrawTopBar() {
    DrawRect(0, 0, ScreenWidth, 30, 0xFF222222);
    CursorX = 10; UINTN oldY = CursorY; CursorY = 7;
    const char* title = "VibeOS v5.0 (Full Release)";
    int cx = 10;
    while (*title) { DrawChar(cx, 7, *title++, 0xFFFFFFFF); cx+=16; }
    CursorY = oldY; CursorX = 10;
}

// --- Main ---

extern "C" EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    SystemTable->BootServices->HandleProtocol(SystemTable->ConsoleOutHandle, &GopGuid, (void**)&Gop);
    
    UINT32 BestMode = 0, MaxWidth = 0;
    for(UINT32 i=0; i < Gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
        UINTN Size;
        Gop->QueryMode(Gop, i, &Size, &Info);
        if(Info->HorizontalResolution > MaxWidth) { MaxWidth = Info->HorizontalResolution; BestMode = i; }
    }
    Gop->SetMode(Gop, BestMode);
    
    UINT32 *InfoPtr = (UINT32*)Gop->Mode->Info;
    ScreenWidth = InfoPtr[1]; ScreenHeight = InfoPtr[2];
    Pitch = Gop->Mode->FrameBufferSize / ScreenHeight / 4; 
    if (Gop->Mode->Info->PixelsPerScanLine > ScreenWidth) Pitch = Gop->Mode->Info->PixelsPerScanLine;
    Framebuffer = (UINT32*)Gop->Mode->FrameBufferBase;

    DrawRect(0, 0, ScreenWidth, ScreenHeight, BG_COLOR);
    DrawTopBar();

    EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    SystemTable->BootServices->HandleProtocol(ImageHandle, &LoadedImageGuid, (void**)&LoadedImage);

    EFI_GUID FsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FS;
    if (SystemTable->BootServices->HandleProtocol(LoadedImage->DeviceHandle, &FsGuid, (void**)&FS) == EFI_SUCCESS) {
        FS->OpenVolume(FS, &RootFS);
    }

    kprint("VibeOS Filesystem Ready.\n", FG_COLOR);
    kprint("Type 'help' for command list.\n\n", FG_COLOR);

    CHAR16 inputBuffer[128];
    UINTN bufferIndex = 0;

    while (1) {
        kprint("root", PROMPT_USER); kprint("@", FG_COLOR); kprint("vibeos", PROMPT_HOST); 
        kprint(":", FG_COLOR); kprint16(CurrentPath, DIR_COLOR); kprint("# ", FG_COLOR);
        
        bufferIndex = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));

        while (1) {
            EFI_INPUT_KEY Key;
            if (SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key) != EFI_SUCCESS) continue;
            
            if (Key.UnicodeChar == L'\r') { kprint("\n", 0); break; }
            if (Key.UnicodeChar == L'\b') { 
                if (bufferIndex > 0) { bufferIndex--; inputBuffer[bufferIndex] = 0; kprint("\b", 0); }
                continue; 
            }
            if (Key.UnicodeChar >= 32 && Key.UnicodeChar < 127) {
                inputBuffer[bufferIndex++] = Key.UnicodeChar;
                char s[2] = {(char)Key.UnicodeChar, 0};
                kprint(s, FG_COLOR);
            }
        }

        if (bufferIndex == 0) continue;

        CHAR16 *Arg = 0;
        for(UINTN i=0; i<bufferIndex; i++) {
            if(inputBuffer[i] == L' ') { inputBuffer[i] = 0; Arg = &inputBuffer[i+1]; break; }
        }

        if (StrCmp(inputBuffer, (CHAR16*)L"help")) CmdHelp();
        else if (StrCmp(inputBuffer, (CHAR16*)L"clear")) Scroll();
        else if (StrCmp(inputBuffer, (CHAR16*)L"ls")) CmdLS();
        else if (StrCmp(inputBuffer, (CHAR16*)L"pwd")) CmdPWD();
        else if (StrCmp(inputBuffer, (CHAR16*)L"neofetch")) CmdNeofetch();
        else if (StrCmp(inputBuffer, (CHAR16*)L"reboot")) SystemTable->RuntimeServices->ResetSystem(0, EFI_SUCCESS, 0, 0);
        else if (StrCmp(inputBuffer, (CHAR16*)L"off")) SystemTable->RuntimeServices->ResetSystem(2, EFI_SUCCESS, 0, 0);
        else if (StrCmp(inputBuffer, (CHAR16*)L"sudo")) kprint("root permission is implied.\n", ERR_COLOR);
        
        else if (StrCmp(inputBuffer, (CHAR16*)L"cd")) { if(Arg) CmdCD(Arg); else CmdPWD(); }
        else if (StrCmp(inputBuffer, (CHAR16*)L"mkdir")) { if(Arg) CmdMkDir(Arg); else kprint("Usage: mkdir <name>\n", ERR_COLOR); }
        else if (StrCmp(inputBuffer, (CHAR16*)L"touch")) { if(Arg) CmdTouch(Arg); else kprint("Usage: touch <name>\n", ERR_COLOR); }
        else if (StrCmp(inputBuffer, (CHAR16*)L"rm")) { if(Arg) CmdRm(Arg); else kprint("Usage: rm <name>\n", ERR_COLOR); }
        else if (StrCmp(inputBuffer, (CHAR16*)L"cat")) { if(Arg) CmdCat(Arg); else kprint("Usage: cat <name>\n", ERR_COLOR); }
        else if (StrCmp(inputBuffer, (CHAR16*)L"edit")) { if(Arg) CmdEdit(Arg, SystemTable); else kprint("Usage: edit <name>\n", ERR_COLOR); }
        else kprint("Unknown command.\n", ERR_COLOR);
    }
    return EFI_SUCCESS;
}
