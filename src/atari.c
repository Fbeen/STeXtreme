/*
 * atari.c
 * 
 * Inspect the Atari ST keyboard serial protocol and remember key presses.
 * 
 * Written by Frank Beentjes <frankbeen@gmail.com> & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Atari keyboard:
 *  A3: Keyboard RX
 */
 
#define ST_SHIFT  0x01
#define ST_CTRL   0x02
#define ST_ALT    0x04
#define ST_HELP   0x08
#define ST_LEFT   0x10
#define ST_RIGHT  0x20
#define ST_SELECT 0x40

uint8_t stKeyboardState = 0; // keep track on the left Control and Alternate keys

uint8_t keyscan_to_ascii(uint8_t key);

void st_init(void)
{
    /* Turn on the clocks. */
    rcc->apb1enr |= RCC_APB1ENR_USART2EN;

    /* Enable RX pin (PA3) as input. */
    gpio_configure_pin(gpioa, 3, GPI_pull_up);

    /* BAUD, 8n1. */
    usart2->brr = 4608; // 36.000.000 / 7812.5
    usart2->cr1 = (USART_CR1_UE | USART_CR1_RE /* | USART_CR1_RXNEIE */ );
}

uint8_t st_check(void) 
{
    uint8_t key;
    
    // Check if RXNE (Read data register not empty) is set
    if(usart2->sr & USART_SR_RXNE) {
    
        key = usart2->dr;
        
//        printk("atari code: %u \n", key);
//        printk("ascii code: %u \n\n", keyscan_to_ascii(key);
        
	switch(key) {
	    case 42: // Shift pressed
		stKeyboardState |= ST_SHIFT;
		break;
	    case 170: // Shift released
		stKeyboardState &= ~(ST_SHIFT);
		break;
	    case 29: // Control pressed
		stKeyboardState |= ST_CTRL;
		break;
	    case 157: // Control released
		stKeyboardState &= ~(ST_CTRL);
		break;
	    case 56: // Alternate pressed
		stKeyboardState |= ST_ALT;
		break;
	    case 184: // Alternate released
		stKeyboardState &= ~(ST_ALT);
		break;
	}
	
	// Always release the Left, Right, Select and Help keys when this keys are released
	switch(key) {
	    case 203: // Left released
		stKeyboardState &= ~(ST_LEFT);
		break;
	    case 205: // Right released
		stKeyboardState &= ~(ST_RIGHT);
		break;
	    case 200: // Select released
		stKeyboardState &= ~(ST_SELECT);
		break;
	    case 226: // Help released
		stKeyboardState &= ~(ST_HELP);
		break;
	}

	// detect Left, Right, Select and Help only when Control and Alternate are held
	// when config mode is active all keys will be forwarded
	if(config_active || (stKeyboardState & ST_CTRL && stKeyboardState & ST_ALT)) {
	    switch(key) {
	        case 75: // Left pressed
		    stKeyboardState |= ST_LEFT;
		    break;
	        case 77: // Right pressed
		    stKeyboardState |= ST_RIGHT;
		    break;
	        case 72: // Select pressed
		    stKeyboardState |= ST_SELECT;
		    break;
	        case 98: // Help pressed
		    stKeyboardState |= ST_HELP;
		    break;
            }
            
	    return key;
	}

    }
    
    return FALSE; // no key press
}

uint8_t getFFbuttons(void) 
{
    uint8_t b = stKeyboardState << 2;
    b = b >> 6;
    
    if(stKeyboardState & ST_HELP)
    	b |= 0x04;
    	
    return b;
}

uint8_t getConfigButtons(void) 
{
    return stKeyboardState >> 4;
}

uint8_t array_search(uint8_t *arr, uint8_t len, uint8_t val)
{
    for(uint8_t i = 0 ; i < len ; i++) {
    	if(arr[i] == val)
    	    return i;
    }
    
    return 0xFF;
}

uint8_t keyscan_to_ascii(uint8_t key)
{
    bool_t caps = stKeyboardState & ST_SHIFT;
    uint8_t idx;
    
    uint8_t alphanumeric[26] = {30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
    			     /* A  B  C  D  E  F  G  H  I  J  K  L  M  N  O  P  Q  R  S  T  U  V  W  X  Y  Z */
    
    uint8_t numeric[10] = {11,2, 3, 4, 5, 6, 7, 8, 9, 10};
    			/* 0  1  2  3  4  5  6  7  8  9 */
    
    uint8_t keypad[10]  = {112,109,110,111,106,107,108,103,104,105};
    			/* 0   1   2   3   4   5   6   7   8   9 */
    			
    switch(key) {
     	case 74: return 45; // -
    	case 78: return 43; // +
    	case 14: return 8;  // backspace
    	case 51: if(caps) return '<'; // <
    		 return 44; // comma
    	case 52: if(caps) return '>'; // >
    		 return 46; // dot
    	case 113: return 46; // dot
    	case 53:
    	case 101: return 47; // slash
    	case 102: return 42; // *
    	case 57: return 32; // space
    	case 10: if(caps) return '('; // (
    	case 11: if(caps) return ')'; // )
    	case 12: if(caps) return 95; // underscore (_)
    }

    idx = array_search(alphanumeric, 26, key);
    if(idx != 0xFF) {
        idx += 65; // caps characters
        if(!caps) {
            idx += 32; // small characters
        }
        return idx;
    }
    
    idx = array_search(numeric, 10, key);
    if(idx != 0xFF) {
        return idx + 48;
    }
    
    idx = array_search(keypad, 10, key);
    if(idx != 0xFF) {
        return idx + 48;
    }
    
    return 0;
}
