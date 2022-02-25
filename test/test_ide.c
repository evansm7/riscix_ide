/* RISC OS test program
 *
 * For use with an ICS/IanS ZIDEFS podule in slot 0!
 *
 * Useful to test partition interpretation w.r.t. HForm, show basic read access working
 * by poking the card regs directly, building portions of the driver.
 */
#include <inttypes.h>
#include <stdio.h>
#include <swis.h>
#include "ecide_io.h"


// From RISCiX headers:
#define XCB_SPEED_SLOW          0
#define XCB_SPEED_MEDIUM        1
#define XCB_SPEED_FAST          2
#define XCB_SPEED_SYNC          3               /* slowest of all...(!) */

#define XCB_SLOT_OFF(slot)      ((slot) << 14)
#define XCB_ADDRESS(speed, slot) \
        (0x03240000 + (speed << 19) + \
         XCB_SLOT_OFF(slot))


void DELAY_(int d)
{
        int i;
        for (i = 0; i < d; i++) {
        }
}

#define A5K 0x3010000 + (0x1f0*4); // Internal IDE on A5000

static uint8_t buffer[512];

ide_host_t      ide;

/* Very preliminary test to differentiate ICS/IanS 8/16b cards */
static void probe_width(regs_t p)
{
        /* Try to differentiate 8-bit from 16-bit podule, as they have the
         * same ROM header.  Method:
         *      - Look for high byte latch at 0x2800.  This might, on the 16b podule,
         *        alias the ROM page register.  (And emulators might not emulate this...)
         *      - Write a value to both to check for this aliasing
         *
         * Ideally don't touch the IDE drive registers themselves (looking for the
         * data register being 16b is the obvious approach, but I don't want to
         * write data that could risk confusing the drive).
         */
        unsigned int w = 0;

        unsigned int r;
        /* First, is there something writable at 2800? */
        write_reg8(p, 0x2800/4, 0xaa);
        if (read_reg8(p, 0x2800/4) != 0xaa) {
                printf("Can't write high latch\n");
                w = 16;
                goto done;
        }
        write_reg8(p, 0x2800/4, 0x55);
        if (read_reg8(p, 0x2800/4) != 0x55) {
                printf("Can't write high latch\n");
                w = 16;
                goto done;
        }
        /* Second, see if that aliases to the ROM latch */
        if (read_reg8(p, 0x2000/4) == 0x55) {
                /* Hmm, suspicious.  Double-check: */
                write_reg8(p, 0x2800/4, 0x69);
                if (read_reg8(p, 0x2000/4) == 0x69) {
                        printf("ROM latch aliases high byte\n");
                        w = 16;
                } else {
                        /* coincidence??! But does not seem to alias,
                         * so still guess 8b.
                         */
                        w = 8;
                }
        } else {
                /* No aliasing, but separate latch - guess 8b card */
                w = 8;
        }
done:
        printf("Card width %d\n", w);
}

int main(void)
{
        int r;
        uint32_t *d = (uint32_t *)buffer;
        int i;

        ide.regs = (regs_t)(XCB_ADDRESS(XCB_SPEED_SLOW, 0) + 0x3000);

        printf("Hello from C! Probing IDE at %p:\n\n", ide.regs);

        probe_width((regs_t)(XCB_ADDRESS(XCB_SPEED_SLOW, 0) + 0x0));

        r = ide_init(&ide, 0, buffer);
        if (r < 0)
                return 1;

        printf("\nReading sector 0:\n");
        ide_read_one(&ide, 0, 0, buffer);

        for (i = 0; i < 512/4; i++) {
                if ((i & 7) == 0)
                        printf("%02x: ", i);
                printf("%08x ", d[i]);
                if ((i & 7) == 7)
                        printf("\n");
        }

        // Read partition table:
        if (ide.drives[0].present)
                ide_probe_partitions(&ide, 0, buffer);
        if (ide.drives[1].present)
                ide_probe_partitions(&ide, 1, buffer);
        ide_dump_partitions(&ide, 0);
        ide_dump_partitions(&ide, 1);

        if (0) {
                printf("\nInverting sector 1:\n");
                ide_read_one(&ide, 0, 1, buffer);
                for (i = 0; i < 512/4; i++) {
                        d[i] ^= i + (0xdeadbeef*i);
                }
                ide_write_one(&ide, 0, 1, buffer);

                printf("\nReading sector 1:\n");
                ide_read_one(&ide, 0, 1, buffer);

                d = (uint32_t *)buffer;
                for (i = 0; i < 512/4; i++) {
                        if ((i & 7) == 0)
                                printf("%02x: ", i);
                        printf("%08x ", d[i]);
                        if ((i & 7) == 7)
                                printf("\n");
                }
        }

        return 0;
}
