/*
 * main.c
 * 
 * Bootstrap the STM32F103C8T6 and get things moving.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* enhanced in/outputs Atari ST */
#define gpio_rom_select gpiob    // gpio port for TOS rom select
#define rom_select_low 5         // lower pin for TOS rom select
#define rom_select_high 4        // higher pin for TOS rom select

#define gpio_boot_select gpioc   // gpio port for boot order select (boot from internal or external floppydrive)
#define boot_order_pin 13        // pin for boot order select

#define gpio_sound_select gpiob  // gpio port for selecting mono/stereo mode
#define sound_select_pin 0       // pin for selecting mono/stereo mode

#define gpio_reset gpiob         // gpio port for reset line
#define reset_pin 3              // reset pin

#define gpio_ff_on gpioa	// gpio port for flashfloppy on line in
#define ff_on_pin 7		// flashfloppy on pin

#define gpio_hd_on gpiob	// gpio port for harddisk on line in
#define hd_on_pin 14		// harddisk on pin

#define gpio_ff_led gpiob	// gpio port for flashfloppy LED
#define ff_led_pin 15		// flashfloppy LED pin

#define gpio_hd_led gpioa	// gpio port for harddisk LED
#define hd_led_pin 8		// harddisk LED pin

static struct display *cur_display = &i2c_display; // i2c_display is initialized in i2c.c
static uint8_t notify_text[2][16];       // notifications shown on the lcd display
static uint8_t notify_time = 0;         // timer value in milliseconds
static uint8_t current_lcd_text[2][16];  // previous text that was sent to the lcd 
bool_t was_bl_on;			 // was the backlight on before we showed our notification?


/* FlashFloppy and Harddisk states and timers */
uint8_t HDState = 0;
uint8_t FFState = 0;
uint8_t hd_timer = 0;
uint8_t ff_timer = 0;

/* bootup delay */
uint8_t bootup = 10; // disable read reset line for 1 second from bootup
uint8_t ld_timer = 5; // let the build in toggle on and off each 0.5 second

/* functions from lcd.c */
extern void lcd_init(void);
extern void lcd_refresh(uint8_t *text, uint8_t ruleNr);
extern bool_t isBacklightOn(void);
extern void backlight(int on);
extern uint8_t getFFbuttons(void);

/* functions from atari.c */
extern void st_init(void);
extern uint8_t st_check(void);

/* start in the main function */
int EXC_reset(void) __attribute__((alias("main")));

/* function prototypes */
void ffLed(uint8_t on);
void hdLedOff(void);
void setPin(GPIO gpio, unsigned int pin, uint8_t state);

/* Guard the stacks with known values. */
static void canary_init(void)
{
    _irq_stackbottom[0] = _thread_stackbottom[0] = 0xdeadbeef;
}

/* Has either stack been clobbered? */
static void canary_check(void)
{
    ASSERT(_irq_stackbottom[0] == 0xdeadbeef);
    ASSERT(_thread_stackbottom[0] == 0xdeadbeef);
}

static void watchdog_init(void)
{
    /* Set up the Watchdog. Based on LSI at 30-60kHz (av. 40kHz). */
    iwdg->kr = 0xcccc; /* Enables watchdog, turns on LSI oscillator. */
    while (iwdg->sr & 3) {
        /* System Memory Bootloader modifies PR. We must wait for that 
         * to take effect before making our own changes. */
    }
    iwdg->kr = 0x5555; /* Enables access to PR and RLR. */
    iwdg->pr = 3;      /* Prescaler: div32 => Ticks at 937-1875Hz (1250Hz) */
    iwdg->rlr = 400;   /* Reload:    400   => Times out in 213-426ms (320ms) */
    iwdg->kr = 0xaaaa; /* Load the new Reload value. */
}

static void watchdog_kick(void)
{
    /* Reload the Watchdog. */
    iwdg->kr = 0xaaaa;
}

/* we use a very basic multi purpose timer that interrups every 0.5 seconds */
#define tim2_irq 28
void IRQ_28(void) __attribute__((alias("IRQ_default_timer")));

/* interrupt Triggered each 0.5 seconds */
uint8_t i = 0;
static void IRQ_default_timer(void)
{
    if ((tim2->sr & 0x0001) != 0) {

	tim2->sr &= ~(1<<0); // Clear UIF update interrupt flag
	
	/* timeout for the notifications shown */
	if(notify_time > 0) {
	    notify_time--;
	    if(notify_time == 0) {
	        /* if the backlight was off before the notification then switch it off again */
	        backlight(was_bl_on);
            }
	}
	
	/* before we switch on the FlashFloppy led we will wait a millisecond to filter out very short pulses.*/
	if(ff_timer > 0) {
	    ff_timer--;
	    if(ff_timer == 0 && gpio_read_pin(gpio_ff_on, ff_on_pin) == 0) { // flashfloppy line in is active low
	        ffLed(1);
            }
	}
	
	/* before we will switch off the Harddisk led we will wait a 0.25 second to avoid very short flickers */
	if(hd_timer > 0) {
	    hd_timer--;
	    if(hd_timer == 0 && gpio_read_pin(gpio_hd_on, hd_on_pin) == 0) {
	        hdLedOff();
            }
	}
	
	/* Bootup delay. If the atari Boots then the reset line will be hold down for a while. this delay avoids that we start with a -- RESET -- notification */
	if(bootup > 0) {
	    bootup--;
	}
	
	ld_timer--;
	if(ld_timer == 0) {
	    // Toggle the build in led to indicate that the board is up and running
	    gpio_write_pin(gpioc, 13, !gpio_read_pin(gpioc, 13));
	    ld_timer = 5;
	    
	    if(config_active) {
	    	/* Blink led Turquoise to indicate config mode */
	    	// setPin(gpio_ff_led, ff_led_pin, 1);
	    	setPin(gpio_hd_led, hd_led_pin, !gpio_read_pin(gpio_hd_led, hd_led_pin));
	    }
	}
    }
}

/* Show a notification on the LCD screen */
void notify(char* line1, char* line2)
{
    uint8_t len1 = strlen(line1);
    uint8_t len2 = strlen(line2);
    
    /* copy the text of each line to the notify-buffer and add additional spaces */
    memcpy(&notify_text[0][0], line1, len1);
    memset(&notify_text[0][len1], ' ', 16-len1);
    
    memcpy(&notify_text[1][0], line2, len2);
    memset(&notify_text[1][len2], ' ', 16-len2);
    
    /* set the notification timer */
    notify_time = 30; // = 3 seconds
    
    /* save the current state of the backlight and switch the backlight on */
    was_bl_on = isBacklightOn();
    backlight(TRUE);
}

/* Inside the Atari ST the reset line is normaly high and if pulled low the ST stays in the reset state */
void holdReset(void) {
    /* Set the pin as output and pull it low for a while to set the Atari ST in the reset state */
    gpio_configure_pin(gpio_reset, reset_pin, GPO_pushpull(_2MHz, LOW));
}

void releaseReset(void) {
    /* Set the pin as input to leave the ST doing its normal thing */
    gpio_configure_pin(gpio_reset, reset_pin, GPI_floating);
}

/* process the key presses from the Atari ST */
uint8_t update_st_keys(void)
{
    uint8_t stKey;	/* key code from the Atari keyboard */
    char text[15] = "Current ROM x:";
	
    /* get keypresses from the Atari (when Control and Alternate keys are pressed */
    stKey = st_check();
	
    /* no keys pressed */
    if(stKey == FALSE) {
        return 0;
    }
    
    if(config_active) {
    	return stKey; // forward the key to the configuration
    }
    
    /* copy pressed keys left, right and up to i2c_osd_info structure so that it will be send back to the FlashFloppy device through i2c */
    *(volatile uint8_t *)&i2c_osd_info.buttons = getFFbuttons();
    
    /* handle keys */
    switch(stKey) {
       // Enhanced Atari ST upgrades
        case 48: // B --> Boot from internal or external drive
            holdReset();
            gpio_write_pin(gpio_boot_select, boot_order_pin, !gpio_read_pin(gpio_boot_select, boot_order_pin));
            delay_ms(250);
            releaseReset();
            if(gpio_read_pin(gpio_boot_select, boot_order_pin)) {
                notify("> Boot from", "  internal drive");
            } else {
                notify("> Boot from", "  external drive");
            }
            break;
        case 83: // Delete  --> Reset the computer
            notify("-- RESET --", "");
            holdReset();
            delay_ms(250);
            releaseReset();
            break;
        case 31: // S --> Select mono or stereo sound
            gpio_write_pin(gpio_sound_select, sound_select_pin, !gpio_read_pin(gpio_sound_select, sound_select_pin));
            if(gpio_read_pin(gpio_sound_select, sound_select_pin)) {
                notify("> Stereo sound!", "");
            } else {
                notify("> Mono sound", "");
            }
            break;
        case 59: // F1
        case 60: // F2
        case 61: // F3
        case 62: // F4  --> Select a TOS version
            stKey -= 59;
            text[12] = stKey + 49;
            notify(text, config.TOStitle[stKey]);
            holdReset();
            gpio_write_pin(gpio_rom_select, rom_select_low, stKey & (1<<0));
            gpio_write_pin(gpio_rom_select, rom_select_high, stKey & (1<<1));
            delay_ms(250);
            releaseReset();
            break;
    }
    
    return stKey; // forward the key to the configuration
}

/* compares the new and previous lcd lines and updates the lcd display when there are changes */
void refreshLcdWhenNeeded(uint8_t *text, uint8_t line)
{
    /* when the display line is the same as last time then leave this function */
    if(strncmp((char*)current_lcd_text[line], (char*)text, 16) == 0)
        return;
        
    /* save the new line so that we can compare it later and refresh one line on the lcd */
    memcpy(&current_lcd_text[line][0], text, 16);
    lcd_refresh(text, line);
}

/* called from the main loop, will update the lcd when needed */
void process_display(void)
{
    if(config_active) {
        refreshLcdWhenNeeded(config_display.text[0], 0); // first rule on the lcd
        refreshLcdWhenNeeded(config_display.text[1], 1); // second rule on the lcd
    } else if(notify_time > 0) { 
        /* there is a notification to be shown */
        refreshLcdWhenNeeded(notify_text[0], 0); // first rule on the lcd
        refreshLcdWhenNeeded(notify_text[1], 1); // second rule on the lcd
    } else {
        /* parse through the text received from the FlashFloppy device */
        refreshLcdWhenNeeded(cur_display->text[1], 0); // first rule on the lcd
        refreshLcdWhenNeeded(cur_display->text[2], 1); // second rule on the lcd
        
        /* parse through the backlight status (on or off) received from the FlashFloppy device */
        if(cur_display->on != isBacklightOn()) {
    	    backlight(cur_display->on);
        }
    }
}

/* Function to switch te LEDs on or off */
void setPin(GPIO gpio, unsigned int pin, uint8_t state) {
    if (state) {
	gpio_configure_pin(gpio, pin, GPO_pushpull(_2MHz, HIGH));
    } else {
	gpio_configure_pin(gpio, pin, GPI_pull_down);
    }
}

/* turn the FlashFloppy active led on or off */
void ffLed(uint8_t on) {
    if(config_active)
    	return;
    	
    if(on) {
	// Only turn the FF green led on when HD is inactive
	if(HDState == 0) {
	    setPin(gpio_ff_led, ff_led_pin, 1);
	}
	FFState = 1; // remember that FlashFloppy is active
    } else {
	// switch off the FF led, FlashFloppy is inactive
	setPin(gpio_ff_led, ff_led_pin, 0);
	FFState = 0;
    }
}

/* turn on the harddisk led */
void hdLedOn(void) {
    if(config_active)
    	return;
    	
    // switch off the FlashFloppy led
    setPin(gpio_ff_led, ff_led_pin, 0);

    // and then switch on the harddisk Led
    setPin(gpio_hd_led, hd_led_pin, 1);
    HDState = 1;
}

/* turn off the harddisk led */
void hdLedOff(void) {
    if(config_active)
    	return;

    // switch off the harddisk led
    setPin(gpio_hd_led, hd_led_pin, 0);
    HDState = 0;

    // if FlashFloppy is active then switch on the FlashFloppy led
    if(FFState) {
	setPin(gpio_ff_led, ff_led_pin, 1);
	FFState = 0;
    }
}

/* process the FlashFloppy and harddisk leds. called from the main loop */
void process_drives(void)
{
    /* FlashFloppy drive */
    if(gpio_read_pin(gpio_ff_on, ff_on_pin) == 0) {
   	if(ff_timer == 0) {
	    ff_timer = 2;
	}
    } else {
	ffLed(0);
	ff_timer = 0;
    }

    /* Harddisk */
    if(gpio_read_pin(gpio_hd_on, hd_on_pin) == 1) {
	if(!HDState) {
	    hdLedOn();
	}
	hd_timer = 2;
    }
}
        
/* configure the io pins */
void init_gpio(void)
{
    uint8_t t = config.tos - 1;
    
    /* enhanced io Atari ST */
    holdReset();
    gpio_configure_pin(gpio_rom_select, rom_select_low, GPO_pushpull(_2MHz, t & (1<<0)));
    gpio_configure_pin(gpio_rom_select, rom_select_high, GPO_pushpull(_2MHz, t & (1<<1)));
    releaseReset();

    gpio_configure_pin(gpio_boot_select, boot_order_pin, GPO_pushpull(_2MHz, HIGH));

    gpio_configure_pin(gpio_sound_select, sound_select_pin, GPI_pull_up);
    // gpio_configure_pin(gpio_reset, reset_pin, GPI_floating);

    gpio_configure_pin(gpio_ff_on, ff_on_pin, GPI_pull_down);
    gpio_configure_pin(gpio_hd_on, hd_on_pin, GPI_pull_down);
    gpio_configure_pin(gpio_ff_led, ff_led_pin, GPI_pull_down);
    gpio_configure_pin(gpio_hd_led, hd_led_pin, GPI_pull_down);
    /* end of enhanced outputs Atari ST */

    /* PC13: Blue Pill Indicator LED (Active Low) */
    gpio_configure_pin(gpioc, 13, GPO_pushpull(_2MHz, LOW));
}

/* main entrance */
int main(void)
{
    uint8_t stKey;
    
    watchdog_init();

    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    canary_init();

    stm32_init();
    //time_init();
    console_init();
    i2c_init();

    /* setup Timer 2: psu clockspeed = 72000000 Hz / prescaler 1000 = 72000 Hz. So if we count until 7200 we have 10 Hz or 100 millisecond */
    rcc->apb1enr |= (1<<0); // Enable clock for TIM2
    tim2->psc = 1000-1;     // Set PSC+1 = 1000
    tim2->arr = 7200;       // Set timer to reset after CNT = 7200
    tim2->dier |= (1<<0);   // Enable timer interrupt generation

    /* enable interrupt for timer 2 */
    IRQx_set_prio(tim2_irq, 1);
    IRQx_set_pending(tim2_irq);
    IRQx_enable(tim2_irq);

    tim2->sr &= ~(1<<0);  // Reset status register
    tim2->cr1 &= ~(1<<0); // Disable timer for now
    tim2->cr1 |= (1<<2); // Only counter overflow/underflow generates an update interrupt
    
    // start timer
    tim2->egr |= (1<<0);  // Reset timer
    tim2->cr1 |= (1<<0); // Enable timer	
    
    config_init();

    init_gpio();
    
    st_init();

    lcd_init();
    
    printk("Main loop:\n\n");
    
    for (;;) {
        watchdog_kick();
        canary_check();
        
        process_drives();
        
	stKey = update_st_keys();
	
        config_process(stKey);
	
	if(!bootup && gpio_read_pin(gpio_reset, reset_pin) == LOW) {
            notify("-- RESET --", "");
	}
		
        i2c_process();
        process_display();
    }

    return 0;
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
