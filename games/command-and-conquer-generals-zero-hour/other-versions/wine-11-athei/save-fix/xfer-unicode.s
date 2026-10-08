# game.dat 1.04: load save files with Unicode strings of either width.
# MIT license, see the README.md of the repository.
#
# XferLoad::xferUnicodeString (0x602110) reads a length byte, then calls
# xferUser(buffer, 2 * length) at 0x602139 (call dword [edx+0xb8]): 2 bytes
# for each character, as wchar_t on Windows. GeneralsX wrote 4 bytes for each
# character before 2026-10-08 (wchar_t on macOS). patch-game-dat.py puts this
# code into the free bytes at the end of .text (0x938DF0) and changes that call
# into "call 0x938DF0". The code finds the width once for each file, at its
# first string with characters, as the GeneralsX reader does: in the 4-byte
# format the bytes 2 and 3 are zero. A 4-byte string is read with the
# original xferUser (so a short file still gives the game's read error) and
# changed in place to 2-byte characters (above 0xFFFF: '?').
# Same rule as games/command-and-conquer-generals-zero-hour/other-versions/native-mac/runtime/llasm/xfer-unicode.c.
#
# Entry: ecx = XferLoad, [esp+4] = buffer, [esp+8] = 2 * length. Exit: ret 8,
# as xferUser. Variables in the free bytes of .data1: 0xA70F00 last FILE,
# 0xA70F04 last position, 0xA70F08 width flag (1: 4 bytes).

        .intel_syntax noprefix
        .text
        .globl start
start:
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        sub esp, 12                      # [ebp-16] probe (4 bytes), [ebp-20] position, [ebp-24] count
        mov esi, ecx                     # this
        mov edi, [ebp+8]                 # buffer
        mov ebx, [ebp+12]                # 2 * length
        test ebx, ebx
        jz plain

        mov eax, [esi+0x10]              # m_fileFP
        test eax, eax
        jz plain
        push eax
        call dword ptr [0x93945C]        # ftell
        add esp, 4
        mov [ebp-20], eax
        test eax, eax
        js plain
        mov edx, [esi+0x10]
        cmp edx, dword ptr [0xA70F00]
        jne decide
        cmp eax, dword ptr [0xA70F04]
        jg known
decide:
        mov dword ptr [0xA70F00], edx
        mov dword ptr [ebp-16], 0
        push edx                         # fread(&probe, 1, 4, fp)
        push 4
        push 1
        lea ecx, [ebp-16]
        push ecx
        call dword ptr [0x939460]        # fread
        add esp, 16
        mov [ebp-24], eax                # count (fseek may change ecx)
        push 0                           # fseek(fp, position, SEEK_SET)
        push dword ptr [ebp-20]
        push dword ptr [esi+0x10]
        call dword ptr [0x939458]        # fseek
        add esp, 12
        xor eax, eax
        cmp dword ptr [ebp-24], 4
        jne setwidth
        cmp word ptr [ebp-14], 0         # probe bytes 2 and 3
        jne setwidth
        inc eax
setwidth:
        mov dword ptr [0xA70F08], eax
known:
        mov eax, [ebp-20]
        mov dword ptr [0xA70F04], eax
        cmp dword ptr [0xA70F08], 0
        je plain

        mov eax, [esi]                   # xferUser(buffer, 4 * length)
        lea edx, [ebx+ebx]
        push edx
        push edi
        mov ecx, esi
        call dword ptr [eax+0xB8]
        mov ecx, ebx
        shr ecx, 1                       # length
        xor edx, edx
convert:
        cmp edx, ecx
        jae done
        mov eax, [edi+edx*4]
        cmp eax, 0xFFFF
        jbe store
        mov eax, 0x3F                    # '?'
store:
        mov [edi+edx*2], ax
        inc edx
        jmp convert

plain:
        mov eax, [esi]                   # the original call: xferUser(buffer, 2 * length)
        push ebx
        push edi
        mov ecx, esi
        call dword ptr [eax+0xB8]
done:
        add esp, 12
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 8
