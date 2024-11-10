/*
 * config.h
 * 
 * User configuration.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

extern struct packed config {
    char TOStitle[4][17];	// TOS names for each bank
    uint8_t  tos; 		// current TOS bank
    uint8_t  sound; 		// mono or stereo sound at startup
    uint8_t  boot; 		// startup with intern floppy or extern floppy

    uint16_t crc16_ccitt;

} config;

extern bool_t config_active;
extern struct display config_display;

void config_init(void);
void config_process(uint8_t stKey);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
