/*
 * config.c
 * 
 * Read/write/modify configuration parameters.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define F(x) (x-1)
#define U(x) (1u<<x)

const static struct config *flash_config = (struct config *)0x0800fc00;

#include "default_config.c"

extern void backlight(int on);
extern void hdLedOff(void);
extern uint8_t getConfigButtons(void);
extern uint8_t keyscan_to_ascii(uint8_t key);
extern void cursor(bool_t on);

struct config config;

#define ST_SELECT 98
#define ST_LEFT 75
#define ST_RIGHT 77


static void config_printk(const struct config *conf)
{
    printk("\nCurrent config:\n");

    printk(" TOS names: \n");
    
    for(uint8_t i = 0 ; i < 4 ; i++) {
    	printk("  F%u: %s\n", i+1, conf->TOStitle[i]);
    }
    
    printk(" Startup TOS: %u\n", conf->tos);
    printk(" Startup sound: %s\n",
           config.sound ? "Mono" : "Stereo");
    printk(" Startup boot from: %s floppydrive\n",
           config.boot ? "intern" : "extern");
}

static void config_write_flash(struct config *conf)
{
    conf->crc16_ccitt = htobe16(
        crc16_ccitt(conf, sizeof(*conf)-2, 0xffff));
    fpec_init();
    fpec_page_erase((uint32_t)flash_config);
    fpec_write(conf, sizeof(*conf), (uint32_t)flash_config);
}

void config_init(void)
{
    uint16_t crc;

    printk("\n** Atari STe Xtreme v%s **\n", fw_ver);
    printk("** Frank Beentjes <frankbeen@gmail.com>\n");
    printk("** Special thanks goes to: Keir Fraser\n");
    printk("** https://github.com/fbeen/stextreme\n");

    config = *flash_config;
    crc = crc16_ccitt(&config, sizeof(config), 0xffff);
    if (crc) {
        printk("\nConfig corrupt: Resetting to Factory Defaults\n");
        config = dfl_config;
    } else if (gpio_pins_connected(gpioa, 1, gpioa, 2)) {
        printk("\nA1-A2 Jumpered: Resetting to Factory Defaults\n");
        config = dfl_config;
        config_write_flash(&config);
    }
    
    config_printk(&config);

    printk("\nKeys:\n Space: Select\n O: Down\n P: Up\n");

    // lcd_display_update();
    (void)usart1->dr;
}

bool_t config_active;
struct display config_display = {
    .cols = 16, .rows = 2, .on = TRUE,
};

static enum {
    C_idle = 0,
    C_banner,
    /* Output */
    C_title1,
    C_title2,
    C_title3,
    C_title4,
    C_tos,
    C_sound,
    C_boot,
    /* Exit */
    C_save,
    C_max
} config_state;

static void cnf_prt(int row, const char *format, ...)
{
    uint8_t len;
    va_list ap;
    char *r = (char *)config_display.text[row];

    memset(r, 0, 20);

    va_start(ap, format);
    (void)vsnprintf(r, 20, format, ap);
    va_end(ap);
    len = strlen(r);
    memset(&r[len], ' ', 16-len);

    printk((row == 0) ? "\n%s%16s " : "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b%16s", r, "");
}

static struct repeat {
    int repeat;
    time_t prev;
} left, right;

uint8_t button_repeat(uint8_t pb, uint8_t b, uint8_t m, struct repeat *r)
{
    if (pb & m) {
        /* Is this button held down? */
        if (b & m) {
            time_t delta = time_ms(r->repeat ? 100 : 500);
            if (time_diff(r->prev, time_now()) > delta) {
                /* Repeat this button now. */
                r->repeat++;
            } else {
                /* Not ready to repeat this button. */
                b &= ~m;
            }
        } else {
            /* Button not pressed. Reset repeat count. */
            r->repeat = 0;
        }
    }
    if (b & m) {
        /* Remember when we actioned this button press/repeat. */
        r->prev = time_now();
    }
    return b;
}

uint8_t arrowKeys(uint8_t key)
{
    time_t start = time_now();
    uint8_t i = 0;
    
    if(key != 27)
        return 0;
    
    while(time_diff(start, time_now()) < time_ms(100)) 
    {
        if (usart1->sr & USART_SR_RXNE) {
            key = usart1->dr;
            //printk("key: %d\n", key);
    	    switch (i) {
    	        case 0:
    	   	    if(key != 91) return 0;
    	        case 1:
                    switch (key) {
                        case 65: return B_SELECT;
                        case 67: return B_RIGHT;
                        case 68: return B_LEFT;
    	            }
            }
            i++;
        }
    }
    
    return 0;
}

void config_process(uint8_t stKey)
{
    char c = 0;
    uint8_t title_idx;
    uint8_t ascii;
    uint8_t l;
    uint8_t b;
    uint8_t _b;
    static uint8_t pb;
    bool_t changed = FALSE;
    static enum { C_SAVE = 0, C_SAVEREBOOT, C_USE, C_DISCARD,
                  C_RESET, C_NC_MAX } new_config;
    static struct config old_config;

    b = getConfigButtons();
    _b = b;
    b &= b ^ (pb & B_SELECT);
    b = button_repeat(pb, b, B_LEFT, &left);
    b = button_repeat(pb, b, B_RIGHT, &right);
    pb = _b;

    ascii = keyscan_to_ascii(stKey);
    if (usart1->sr & USART_SR_RXNE) {
        c = usart1->dr;
        // printk("key: %d\n", c);
        b = arrowKeys(c);
        if(!b && c>=32 && c<=125)
            ascii = c;
        if(c==127)
            ascii = 8;
    }

    if (b & B_SELECT) {
        if (++config_state >= C_max) {
            config_state = C_idle;
            switch (new_config) {
            case C_SAVE:
                config_write_flash(&config);
                break;
            case C_SAVEREBOOT:
                config_write_flash(&config);
                while(1) {} /* hang and let WDT reboot */
                break;
            case C_USE:
                break;
            case C_DISCARD:
                config = old_config;
                break;
            case C_RESET:
                config = dfl_config;
                config_write_flash(&config);
                while(1) {} /* hang and let WDT reboot */
                break;
            case C_NC_MAX:
                break;
            }
            printk("\n");
            config_printk(&config);
            // lcd_display_update();
        }
        config_active = (config_state != C_idle);
        if(!config_active)
            hdLedOff();
        changed = TRUE;
    }

    switch (config_state) {
    default:
        break;
    case C_banner:
        if (changed) {
            cnf_prt(0, "Atari STe Xtreme");
            cnf_prt(1, "Configuration");
            old_config = config;
            backlight(1);
        }
        break;
    case C_title1:
    case C_title2:
    case C_title3:
    case C_title4:
        
    	title_idx = config_state - C_title1;
        if (changed)
            cnf_prt(0, "ROM %u name:", title_idx+1);
        if(changed || ascii > 0) {
            l = strlen(config.TOStitle[title_idx]);
            if(ascii == 8) {
                if(l > 0)
                    config.TOStitle[title_idx][l-1] = 0;
            } else if(l < 16 && ascii > 0) {
                config.TOStitle[title_idx][l] = ascii;
                config.TOStitle[title_idx][l+1] = 0;
            }
            cnf_prt(1, "%s", config.TOStitle[title_idx]);
        }
        break;
    case C_tos:
        if (changed)
            cnf_prt(0, "TOS rom (1-4):");
        if (b & B_LEFT)
            config.tos = max_t(uint16_t, config.tos-1, 1);
        if (b & B_RIGHT)
            config.tos = min_t(uint16_t, config.tos+1, 4);
        if (b)
            cnf_prt(1, "%u", config.tos);
        break;
    case C_sound:
        if (changed)
            cnf_prt(0, "Sound:");
        if (b & (B_LEFT|B_RIGHT)) {
            config.sound ^= 1;
        }
        if (b)
            cnf_prt(1, "%s", config.sound ? "Mono" : "Stereo");
        break;
    case C_boot:
        if (changed)
            cnf_prt(0, "Boot:");
        if (b & (B_LEFT|B_RIGHT)) {
            config.boot ^= 1;
        }
        if (b)
            cnf_prt(1, "%stern", config.boot ? "In" : "Ex");
        break;
    case C_save: {
        const static char *str[] = { "Save", "Save+Reset", "Use",
                                     "Discard", "Factory Reset" };
        if (changed) {
            cnf_prt(0, "Save new Config?");
            new_config = C_SAVEREBOOT;
        }
        if (b & B_LEFT) {
            if (new_config > 0)
                --new_config;
            else
                new_config = C_NC_MAX-1;
        }
        if (b & B_RIGHT)
            if (++new_config >= C_NC_MAX)
                new_config = 0;
        if (b)
            cnf_prt(1, "%s", str[new_config]);
        break;
    }
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

