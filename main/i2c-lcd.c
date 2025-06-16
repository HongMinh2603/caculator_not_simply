#include "i2c-lcd.h"
#include <stdio.h>
#include <unistd.h>

#define SLAVE_ADDRESS_LCD 0x4E>>1 // I2C address of LCD (shifted right by 1)
#define I2C_SCL_PIN 19    // SCL pin (modify as needed)
#define I2C_SDA_PIN 18    // SDA pin (modify as needed)
#define I2C_DELAY_US 5    // Delay for 100 kHz I2C clock (5us half-cycle)

static const char *TAG = "LCD";
static int err;

// GPIO register definitions for ESP32
#define GPIO_OUT_REG    (0x3FF44004 + ((I2C_SDA_PIN >= 32) ? 4 : 0)) // GPIO_OUT or GPIO_OUT1
#define GPIO_IN_REG     (0x3FF4403C + ((I2C_SDA_PIN >= 32) ? 4 : 0)) // GPIO_IN or GPIO_IN1
#define GPIO_ENABLE_REG (0x3FF44020 + ((I2C_SDA_PIN >= 32) ? 4 : 0)) // GPIO_ENABLE or GPIO_ENABLE1
#define GPIO_PIN0_REG   (0x3FF44088) // Base address for GPIO_PINx_REG

// Bit masks for SDA and SCL
#define SDA_BIT (1 << (I2C_SDA_PIN % 32))
#define SCL_BIT (1 << (I2C_SCL_PIN % 32))

// Macro for direct register access
#define REG_READ(reg) (*(volatile uint32_t *)(reg))
#define REG_WRITE(reg, val) (*(volatile uint32_t *)(reg) = (val))
#define REG_SET_BIT(reg, bit) REG_WRITE(reg, REG_READ(reg) | (bit))
#define REG_CLEAR_BIT(reg, bit) REG_WRITE(reg, REG_READ(reg) & ~(bit))

// Software I2C functions
static void i2c_delay(void) {
    usleep(I2C_DELAY_US);
}

static void i2c_sda_high(void) {
    REG_WRITE(GPIO_ENABLE_REG, REG_READ(GPIO_ENABLE_REG) & ~SDA_BIT); // Input mode (pull-up sets high)
}

static void i2c_sda_low(void) {
    REG_WRITE(GPIO_ENABLE_REG, REG_READ(GPIO_ENABLE_REG) | SDA_BIT); // Output mode
    REG_WRITE(GPIO_OUT_REG, REG_READ(GPIO_OUT_REG) & ~SDA_BIT);     // Set low
}

static void i2c_scl_high(void) {
    REG_WRITE(GPIO_ENABLE_REG, REG_READ(GPIO_ENABLE_REG) & ~SCL_BIT); // Input mode (pull-up sets high)
}

static void i2c_scl_low(void) {
    REG_WRITE(GPIO_ENABLE_REG, REG_READ(GPIO_ENABLE_REG) | SCL_BIT); // Output mode
    REG_WRITE(GPIO_OUT_REG, REG_READ(GPIO_OUT_REG) & ~SCL_BIT);    // Set low
}

static int i2c_read_sda(void) {
    return (REG_READ(GPIO_IN_REG) & SDA_BIT) ? 1 : 0;
}

static void i2c_init_pins(void) {
    // Configure SDA and SCL as input initially (rely on pull-ups)
    REG_WRITE(GPIO_ENABLE_REG, REG_READ(GPIO_ENABLE_REG) & ~(SDA_BIT | SCL_BIT));

    // Enable internal pull-ups (assuming external pull-ups are present)
    REG_SET_BIT(GPIO_PIN0_REG + I2C_SDA_PIN * 4, 1 << 10); // GPIO_PIN_PAD_PULLUP
    REG_SET_BIT(GPIO_PIN0_REG + I2C_SCL_PIN * 4, 1 << 10); // GPIO_PIN_PAD_PULLUP
}

static void i2c_start(void) {
    i2c_sda_high();
    i2c_scl_high();
    i2c_delay();
    i2c_sda_low();
    i2c_delay();
    i2c_scl_low();
    i2c_delay();
}

static void i2c_stop(void) {
    i2c_scl_low();
    i2c_sda_low();
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    i2c_sda_high();
    i2c_delay();
}

static int i2c_write_bit(uint8_t bit) {
    i2c_scl_low();
    if (bit) {
        i2c_sda_high();
    } else {
        i2c_sda_low();
    }
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    i2c_scl_low();
    return 0; // Success
}

static int i2c_read_bit(void) {
    int bit;
    i2c_scl_low();
    i2c_sda_high(); // Release SDA for slave to control
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    bit = i2c_read_sda();
    i2c_scl_low();
    i2c_delay();
    return bit;
}

static int i2c_write_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        i2c_write_bit((byte >> i) & 1);
    }
    // Read ACK
    return i2c_read_bit() == 0 ? 0 : -1; // 0 for success, -1 for failure
}

static int i2c_master_write(uint8_t *data, size_t len) {
    i2c_start();
    // Write slave address (write mode)
    if (i2c_write_byte((SLAVE_ADDRESS_LCD << 1) | 0) != 0) {
        i2c_stop();
        return -1; // Failure
    }
    // Write data
    for (size_t i = 0; i < len; i++) {
        if (i2c_write_byte(data[i]) != 0) {
            i2c_stop();
            return -1; // Failure
        }
    }
    i2c_stop();
    return 0; // Success
}

void lcd_send_cmd(char cmd) {
    char data_u, data_l;
    uint8_t data_t[4];
    data_u = (cmd & 0xf0);
    data_l = ((cmd << 4) & 0xf0);
    data_t[0] = data_u | 0x0C; // en=1, rs=0
    data_t[1] = data_u | 0x08; // en=0, rs=0
    data_t[2] = data_l | 0x0C; // en=1, rs=0
    data_t[3] = data_l | 0x08; // en=0, rs=0
    err = i2c_master_write(data_t, 4);
    if (err != 0) printf("%s: Error in sending command\n", TAG);
}

void lcd_send_data(char data) {
    char data_u, data_l;
    uint8_t data_t[4];
    data_u = (data & 0xf0);
    data_l = ((data << 4) & 0xf0);
    data_t[0] = data_u | 0x0D; // en=1, rs=1
    data_t[1] = data_u | 0x09; // en=0, rs=1
    data_t[2] = data_l | 0x0D; // en=1, rs=1
    data_t[3] = data_l | 0x09; // en=0, rs=1
    err = i2c_master_write(data_t, 4);
    if (err != 0) printf("%s: Error in sending data\n", TAG);
}

void lcd_clear(void) {
    lcd_send_cmd(0x01);
    usleep(5000);
}

void lcd_put_cur(int row, int col) {
    switch (row) {
        case 0:
            col |= 0x80;
            break;
        case 1:
            col |= 0xC0;
            break;
    }
    lcd_send_cmd(col);
}

void lcd_init(void) {
    i2c_init_pins(); // Initialize GPIO pins for I2C
    usleep(50000);   // Wait for >40ms
    lcd_send_cmd(0x30);
    usleep(5000);    // Wait for >4.1ms
    lcd_send_cmd(0x30);
    usleep(200);     // Wait for >100us
    lcd_send_cmd(0x30);
    usleep(10000);
    lcd_send_cmd(0x20); // 4-bit mode
    usleep(10000);
    lcd_send_cmd(0x28); // Function set: 4-bit, 2 lines, 5x8 chars
    usleep(1000);
    lcd_send_cmd(0x08); // Display off
    usleep(1000);
    lcd_send_cmd(0x01); // Clear display
    usleep(2000);
    lcd_send_cmd(0x06); // Entry mode: increment cursor, no shift
    usleep(1000);
    lcd_send_cmd(0x0C); // Display on, cursor off, blink off
    usleep(1000);
}

void lcd_send_string(char *str) {
    while (*str) lcd_send_data(*str++);
}