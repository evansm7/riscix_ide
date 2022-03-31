/* RISC OS test program
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

int main(void)
{
        int r;
        uint32_t *d = (uint32_t *)buffer;
        int i;

        ide.regs = (regs_t)(XCB_ADDRESS(XCB_SPEED_SLOW, 0) + 0x3000);

        printf("Hello from C! Probing IDE at %p:\n\n", ide.regs);

        r = ide_init(&ide);
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
                ide_probe_partitions(&ide, 0);
        if (ide.drives[1].present)
                ide_probe_partitions(&ide, 1);
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
