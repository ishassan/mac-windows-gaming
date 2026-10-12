/*
 *  Commandos: Behind Enemy Lines: end the hold of a briefing voice step
 *  soon after its voice ends.
 *  MIT license, see the README.md of the repository.
 *
 *  A briefing script (BRIEFING in WARGAME.DIR) is a list of steps. The
 *  number before a step is a count of frames: the next step starts when it
 *  is zero. 0x589BF0 counts it down, once in each frame. The first voice of
 *  each mission briefing has a count, for example
 *  "1800 STARTWAVE #B02H.WAV" in mission 2. The frame limiter of the game
 *  (0x647540) keeps the briefing at 20 frames per second, so 1800 frames
 *  are 90 seconds, but the voice B02H.WAV is 22 seconds long. The picture
 *  and the text then stayed for 68 seconds without sound, and the next
 *  voice started after that. The other briefings have counts of 600 to
 *  1200 frames for voices of 13 to 38 seconds.
 *
 *  The replacement of 0x589BF0 (srw/llasm/instruction_replacements.sci)
 *  calls commandos_briefing_countdown. When the step is a voice step and
 *  its voice has ended, the count is set to HOLD_FRAMES if it is larger.
 *  Other steps count as before.
 *
 *  A voice step (STARTWAVE, STARTWAVEM) keeps its channel at +0x44. When
 *  the voice starts, the step is put into the channel slot of the script
 *  (script + 0x54 + 4 * channel, set at 0x58D1C0). The slot is cleared when
 *  the voice ends: the WAITWAVE step (0x58A010) waits for that. The voice
 *  step class has no fixed address here (the data is relocated), so the
 *  function learns its vtable from the first step that it finds in a slot.
 */

#include <stdint.h>
#include "llasm_cpu.h"
#include "guest.h"

/* Frames (20 each second) that a picture stays after its voice ends */
#define HOLD_FRAMES 40

#define STEP_SCRIPT   0x04
#define STEP_COUNT    0x0C
#define STEP_CHANNEL  0x44
#define SCRIPT_SLOTS  0x54
#define CHANNELS      5

static uint32_t voice_vtable;

/* The countdown of a briefing step: returns 1 while the step must wait. */
EXTERNC uint32_t CCALL commandos_briefing_countdown(uint32_t self)
{
    uint8_t *step = (uint8_t *)from_guest(self);
    int32_t count = (int32_t)rd32(step + STEP_COUNT) - 1;

    if (count > HOLD_FRAMES)
    {
        uint32_t vtable = rd32(step);
        uint32_t channel = rd32(step + STEP_CHANNEL);
        uint8_t *script = (uint8_t *)from_guest(rd32(step + STEP_SCRIPT));
        if (script != NULL && channel < CHANNELS)
        {
            uint32_t slot = rd32(script + SCRIPT_SLOTS + 4 * channel);
            if (slot == self) voice_vtable = vtable;   /* the voice plays */
            else if (vtable == voice_vtable) count = HOLD_FRAMES;   /* the voice ended */
        }
    }
    wr32(step + STEP_COUNT, (uint32_t)count);
    return count > 0;
}
