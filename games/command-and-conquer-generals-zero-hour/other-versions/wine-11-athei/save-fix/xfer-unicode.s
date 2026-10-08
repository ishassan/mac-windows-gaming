# game.dat 1.04: load save files with Unicode strings of either width.
# MIT license, see the README.md of the repository.
#
# XferLoad::xferUnicodeString (0x602110) reads a length byte, then calls
# xferUser(buffer, 2 * length) at 0x602139 (call dword [edx+0xb8]): 2 bytes
# for each character, as wchar_t on Windows. GeneralsX wrote 4 bytes for each
# character before 2026-10-08 (wchar_t on macOS). patch-game-dat.py puts this
# code into the free bytes at the end of .text (0x938DF0) and changes that call
# into "call 0x938DF0". A 4-byte string is read with the original xferUser (so
# a short file still gives the game's read error) and changed in place to
# 2-byte characters (above 0xFFFF: '?').
#
# The width of a file is guessed at its save description, the first string of
# a save, as the GeneralsX reader does: in the 4-byte format the bytes 2 and 3
# are zero. The description starts before 0x200 (0x2B, or 0x45 in a "Mission
# Start" save); all other strings start after the map data of the save, far
# after 0x200. So: a guess for each string that starts before 0x200, and for
# the first string of a FILE that has no width yet. All other strings keep the
# width of their FILE. Two FILEs keep a width at the same time, so the read
# of another save while a save loads does not change the width of the first.
# (The rule before 2026-10-09 guessed again at each change of FILE and at each
# position that was not above the last one: then a short string in the middle
# of a 2-byte save could look like 4-byte text.)
# Same rule as games/command-and-conquer-generals-zero-hour/other-versions/native-mac/runtime/llasm/xfer-unicode.c.
#
# Entry: ecx = XferLoad, [esp+4] = buffer, [esp+8] = 2 * length. Exit: ret 8,
# as xferUser. Variables in the free bytes of .data1 (0xA70F00): two slots of
# FILE and width flag (1: 4 bytes) at +0 and +8, and at +0x10 the address of
# the slot of the last call.

        .intel_syntax noprefix
        .text
        .globl start
        .set SLOTS, 0xA70F00
        .set LAST, 0xA70F10
start:
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        push 0                           # [ebp-16] probe (4 bytes), [ebp-20] position, [ebp-24] count
        push eax
        push eax
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
        pop ecx
        mov [ebp-20], eax
        test eax, eax
        js plain

        mov ecx, [esi+0x10]              # the slot of this FILE
        mov edx, SLOTS
        cmp ecx, [edx]
        je found
        add edx, 8
        cmp ecx, [edx]
        je found
        cmp edx, [LAST]                  # a new FILE: the slot that the last call did not use
        jne take
        sub edx, 8
take:
        mov [edx], ecx
        jmp guess
found:
        cmp eax, 0x200
        jae known
guess:
        push edx
        push ecx                         # fread(&probe, 1, 4, fp)
        push 4
        push 1
        lea eax, [ebp-16]
        push eax
        call dword ptr [0x939460]        # fread
        add esp, 16
        mov [ebp-24], eax                # count (fseek may change ecx)
        push 0                           # fseek(fp, position, SEEK_SET)
        push dword ptr [ebp-20]
        push dword ptr [esi+0x10]
        call dword ptr [0x939458]        # fseek
        add esp, 12
        pop edx
        xor eax, eax
        cmp dword ptr [ebp-24], 4
        jne setwidth
        cmp word ptr [ebp-14], 0         # probe bytes 2 and 3
        jne setwidth
        inc eax
setwidth:
        mov [edx+4], eax
known:
        mov [LAST], edx
        cmp dword ptr [edx+4], 0
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
