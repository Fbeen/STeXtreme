
/* PCF8574 pin assignment: D7-D6-D5-D4-BL-EN-RW-RS */
#define _D7 (1u<<7)
#define _D6 (1u<<6)
#define _D5 (1u<<5)
#define _D4 (1u<<4)
#define _BL (1u<<3)
#define _EN (1u<<2)
#define _RW (1u<<1)
#define _RS (1u<<0)

static uint8_t bl_on = 0x00;

const uint8_t i2c_slave_addr = 0x27;

/* STM32 I2C peripheral. */
static volatile struct i2c *i2c = i2c2;

const static struct i2c_cfg {
    uint8_t en;
    uint8_t scl;
    uint8_t sda;
    uint8_t error_irq;
    uint8_t event_irq;
} i2c2_cfg = {
    .en = 22, /* RCC_APB1ENR_I2C2EN */
    .scl = 10,
    .sda = 11,
    .error_irq = 34,
    .event_irq = 33,
}, *i2c_cfg;

#define SCL2 i2c_cfg->scl
#define SDA2 i2c_cfg->sda

/* Wait for given status condition @s while also checking for errors. */
static bool_t i2c_wait(uint8_t s)
{
    stk_time_t t = stk_now();
    while ((i2c->sr1 & s) != s) {
        if (i2c->sr1 & I2C_SR1_ERRORS) {
            i2c->sr1 &= ~I2C_SR1_ERRORS;
            return FALSE;
        }
        if (stk_diff(t, stk_now()) > stk_ms(10)) {
            /* I2C bus seems to be locked up. */
            return FALSE;
        }
    }
    return TRUE;
}

/* Synchronously transmit the I2C START sequence. 
 * Caller must already have asserted I2C_CR1_START. */
#define I2C_RD TRUE
#define I2C_WR FALSE
static bool_t i2c_start(uint8_t a, bool_t rd)
{
    if (!i2c_wait(I2C_SR1_SB))
        return FALSE;
    i2c->dr = (a << 1) | rd;
    if (!i2c_wait(I2C_SR1_ADDR))
        return FALSE;
    (void)i2c->sr2;
    return TRUE;
}

/* Synchronously transmit the I2C STOP sequence. */
static void i2c_stop(void)
{
    i2c->cr1 |= I2C_CR1_STOP;
    while (i2c->cr1 & I2C_CR1_STOP)
        continue;
}

/* Synchronously transmit an I2C byte. */
static bool_t i2c_sync_write(uint8_t b)
{
    i2c->dr = b;
    return i2c_wait(I2C_SR1_BTF); // Byte transfer finished
}

/* Write a 4-bit nibble over D7-D4 (4-bit bus). */
static void write4(uint8_t val)
{
    i2c_sync_write(val);
    i2c_sync_write(val | _EN);
    i2c_sync_write(val);
}

static void writeNibbles(uint8_t val, uint8_t signals)
{
    write4((val & 0xf0) | signals);
    write4((val << 4) | signals);
}

static void writeText(uint8_t *text, uint8_t len, uint8_t signals)
{
    for(uint8_t i = 0 ; i < len ; i++) {
        write4((text[i] & 0xf0) | signals);
        write4((text[i] << 4) | signals);
    }
}

bool_t lcd_init(void)
{
    i2c = i2c2;
    i2c_cfg = &i2c2_cfg;
    rcc->apb1enr |= 1<<i2c_cfg->en;

    gpio_configure_pin(gpiob, SCL2, AFO_opendrain(_2MHz));
    gpio_configure_pin(gpiob, SDA2, AFO_opendrain(_2MHz));

    /* Standard Mode (100kHz) */
    i2c->cr1 = 0;
    i2c->cr2 = I2C_CR2_FREQ(36);  // set Peripheral clock frequency
    i2c->ccr = I2C_CCR_CCR(180);  // set Clock control register 
    i2c->trise = 37;		  // Maximum rise time in Fm/Sm mode (Master mode)
    i2c->cr1 = I2C_CR1_PE;        // Enable Peripheral

    i2c->cr1 |= I2C_CR1_START; // generate a Start condition
    if (!i2c_start(i2c_slave_addr, I2C_WR))
        goto fail;

    writeNibbles (0x02, 0);       /* 4bit mode */
    writeNibbles (0x28, 0);       /* Initialization of 16X2 LCD in 4bit mode */
    bl_on = 0x08;
    writeNibbles (0x0C, bl_on); /* Display ON Cursor OFF */
    writeNibbles (0x06, bl_on); /* Auto Increment cursor */
    writeNibbles (0x01, bl_on); /* clear display */
    
    i2c_stop();
    
    delay_ms(5);
    
    return TRUE;

fail:
    printk("i2c error!\n\n");
    return FALSE;
}

void lcd_refresh(uint8_t *text, uint8_t ruleNr)
{
    i2c->cr1 |= I2C_CR1_START; // generate a Start condition
    if (!i2c_start(i2c_slave_addr, I2C_WR))
        goto fail;
        
    if(!ruleNr) {
    	writeNibbles (0x80, bl_on);       /* cursor at home position 1st line */
    } else {
        writeNibbles (0xC0, bl_on);       /* cursor at home position 2nd line */
    }
    
    writeText(text, 16, bl_on | _RS);
    
    i2c_stop();
    
    delay_ms(1);

fail:
    return;
    // printk("i2c error!\n\n");
}

bool_t isBacklightOn(void)
{
    return bl_on == 0x08;
}

void backlight(int on)
{
    i2c->cr1 |= I2C_CR1_START; // generate a Start condition
    i2c_start(i2c_slave_addr, I2C_WR);

    if(on) {
        bl_on = 0x08;
        writeNibbles (0x0C, bl_on); /* display on */
    } else {
        bl_on = 0x00;
        writeNibbles (0x08, bl_on); /* display off */
    }
        
    i2c_stop();
    
    delay_ms(1);
}

