#include "stm32f4xx.h"
#include <stdio.h>
#include <stdint.h>

/* Temperature thresholds */
#define COLD_THRESH 32.0f
#define HOT_THRESH  37.0f

/* Servo sweep limits */
#define SERVO_MIN_ANGLE 30
#define SERVO_MAX_ANGLE 150

/* SSD1306 I2C Address */
#define SSD1306_I2C_ADDR 0x3C

/* Global variables */
float temperature = 0.0f;
uint16_t adc_value = 0;

/* Fan modes */
typedef enum{
    FAN_STOP = 0,
    FAN_LOW,
    FAN_NORMAL,
    FAN_HIGH
} FanMode;

FanMode fan_mode = FAN_LOW;
uint8_t control_mode = 0; /* 0 = AUTO (LM35), 1 = MANUAL (Bluetooth) */

/* Compact 5x7 Pixel Font Lookup Table for OLED Text */
typedef struct {
    char c;
    uint8_t data[5];
} FontChar;

const FontChar mini_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}}
};

/* Function declarations */
void init_GPIO(void);
void init_ADC1(void);
void init_TIM3_PWM(void);
void init_USART1(void);
void init_I2C1(void);

uint16_t read_ADC(void);
float read_temperature(void);
void set_servo_angle(uint16_t angle);
void UART_send_string(char *str);
void check_bluetooth_cmd(void);
void delay_ms(uint32_t ms);
void determine_fan_mode(void);
void control_servo(void);

/* SSD1306 Functions */
void I2C_WriteMulti(uint8_t address, uint8_t *data, uint16_t size);
void SSD1306_WriteCommand(uint8_t command);
void SSD1306_Init(void);
void SSD1306_Clear(void);
void SSD1306_SetCursor(uint8_t page, uint8_t col);
void SSD1306_WriteChar(char ch);
void SSD1306_WriteString(uint8_t page, uint8_t col, char *str);

/* Approximate software delay */
void delay_ms(uint32_t ms){
    volatile uint32_t i, j;
    for (i = 0; i < ms; i++){
        for (j = 0; j < 4000; j++) { }
    }
}

/* GPIO initialization */
void init_GPIO(void){
    RCC->AHB1ENR |= (1U << 0);  /* GPIOA clock */
    RCC->AHB1ENR |= (1U << 1);  /* GPIOB clock */

    /* PA0: Analog input for LM35[cite: 1] */
    GPIOA->MODER &= ~(3U << 0);
    GPIOA->MODER |=  (3U << 0);
    GPIOA->PUPDR &= ~(3U << 0);

    /* PA6: TIM3_CH1 alternate function (Servo)[cite: 1] */
    GPIOA->MODER &= ~(3U << 12);
    GPIOA->MODER |=  (2U << 12);
    GPIOA->AFR[0] &= ~(0xFU << 24);
    GPIOA->AFR[0] |=  (2U << 24);

    /* PA9 and PA10: USART1 alternate function (HC-05 Bluetooth)[cite: 1] */
    GPIOA->MODER &= ~((3U << 18) | (3U << 20));
    GPIOA->MODER |=  ((2U << 18) | (2U << 20));
    GPIOA->AFR[1] &= ~((0xFU << 4) | (0xFU << 8));
    GPIOA->AFR[1] |=  ((7U << 4) | (7U << 8));

    /* PB6 (SCL) and PB7 (SDA): I2C1 alternate function (AF4) for OLED[cite: 1] */
    GPIOB->MODER &= ~((3U << 12) | (3U << 14));
    GPIOB->MODER |=  ((2U << 12) | (2U << 14));
    GPIOB->OTYPER |= (1U << 6) | (1U << 7); /* Open-drain */
    GPIOB->PUPDR &= ~((3U << 12) | (3U << 14));
    GPIOB->PUPDR |=  ((1U << 12) | (1U << 14)); /* Pull-up */
    GPIOB->AFR[0] &= ~((0xFU << 24) | (0xFU << 28));
    GPIOB->AFR[0] |=  ((4U << 24) | (4U << 28)); /* AF4 */
}

/* ADC1 initialization */
void init_ADC1(void){
    RCC->APB2ENR |= (1U << 8);
    ADC1->CR1 = 0;
    ADC1->CR2 = 0;
    ADC1->SQR1 = 0;
    ADC1->SQR3 = 0;
    ADC1->SMPR2 |= (7U << 0);
    ADC1->CR2 |= (1U << 0);
}

/* Read ADC */
uint16_t read_ADC(void){
    ADC1->SR = 0;
    ADC1->CR2 |= (1U << 30);
    while (!(ADC1->SR & (1U << 1))) { }
    return (uint16_t)ADC1->DR;
}

/* Convert ADC reading to temperature */
float read_temperature(void)
{
    adc_value = read_ADC();
    return ((float)adc_value * 3.3f * 100.0f) / 4095.0f;
}

/* TIM3 PWM initialization (16MHz clock) */
void init_TIM3_PWM(void){
    RCC->APB1ENR |= (1U << 1);
    TIM3->PSC = 15;        /* 16MHz / 16 = 1MHz (1us ticks) */
    TIM3->ARR = 19999;     /* 20000 ticks = 20ms (50Hz) */
    TIM3->CCMR1 &= ~(7U << 4);
    TIM3->CCMR1 |=  (6U << 4);
    TIM3->CCMR1 |=  (1U << 3);
    TIM3->CCER |= (1U << 0);
    TIM3->CCR1 = 1500;
    TIM3->CR1 |= (1U << 7);
    TIM3->EGR |= (1U << 0);
    TIM3->CR1 |= (1U << 0);
}

/* Set servo angle */
void set_servo_angle(uint16_t angle){
    uint32_t pulse;
    if (angle > 180) angle = 180;
    pulse = 1000U + ((uint32_t)angle * 1000U) / 180U;
    TIM3->CCR1 = pulse;
}

/* USART1 initialization (16MHz clock, 9600 Baud) */
void init_USART1(void){
    RCC->APB2ENR |= (1U << 4);
    USART1->BRR = 0x0683;
    USART1->CR1 = 0;
    USART1->CR1 |= (1U << 3);  /* TE: Transmitter enable */
    USART1->CR1 |= (1U << 2);  /* RE: Receiver enable */
    USART1->CR1 |= (1U << 13); /* UE: USART enable */
}

/* Bluetooth transmission */
void UART_send_string(char *str){
    while (*str)
    {
        while (!(USART1->SR & (1U << 7))) { }
        USART1->DR = (uint8_t)*str++;
    }
}

/* Non-blocking check for incoming Bluetooth commands */
void check_bluetooth_cmd(void){
    if (USART1->SR & (1U << 5)) /* Check RXNE flag */
    {
        char cmd = (char)(USART1->DR & 0xFF);

        if (cmd >= 'a' && cmd <= 'z'){
            cmd -= 32; /* Convert lowercase to uppercase */
        }

        if (cmd == 'A') {
            control_mode = 0;
            UART_send_string(">> Switched to AUTO Mode\r\n");
        }
				
        else if (cmd == 'L') {
            control_mode = 1;
            fan_mode = FAN_LOW;
            UART_send_string(">> Manual: FAN LOW\r\n");
        }
        else if (cmd == 'N') {
            control_mode = 1;
            fan_mode = FAN_NORMAL;
            UART_send_string(">> Manual: FAN NORMAL\r\n");
        }
        else if (cmd == 'H') {
            control_mode = 1;
            fan_mode = FAN_HIGH;
            UART_send_string(">> Manual: FAN HIGH\r\n");
        }
        else if (cmd == 'S') {
            control_mode = 1;
            fan_mode = FAN_STOP;
            UART_send_string(">> Manual: FAN STOPPED\r\n");
        }
    }
}

/* I2C1 Initialization for OLED (16MHz clock) */
void init_I2C1(void){
    RCC->APB1ENR |= (1U << 21);
    I2C1->CR1 = (1U << 15);
    I2C1->CR1 &= ~(1U << 15);
    I2C1->CR2 = 16;
    I2C1->CCR = 80;
    I2C1->TRISE = 17;
    I2C1->CR1 |= (1U << 0);
}

/* Basic I2C Master Write */
void I2C_WriteMulti(uint8_t address, uint8_t *data, uint16_t size){
    volatile uint32_t tmpreg;
    while (I2C1->SR2 & (1U << 1));

    I2C1->CR1 |= (1U << 8);
    while (!(I2C1->SR1 & (1U << 0)));

    I2C1->DR = (address << 1);
    while (!(I2C1->SR1 & (1U << 1)));
    tmpreg = I2C1->SR2;
    (void)tmpreg;

    for (uint16_t i = 0; i < size; i++)
    {
        while (!(I2C1->SR1 & (1U << 7)));
        I2C1->DR = data[i];
    }
    while (!(I2C1->SR1 & (1U << 2)));
    I2C1->CR1 |= (1U << 9);
}

/* Send Command to SSD1306 */
void SSD1306_WriteCommand(uint8_t command){
    uint8_t buf[2];
    buf[0] = 0x00;
    buf[1] = command;
    I2C_WriteMulti(SSD1306_I2C_ADDR, buf, 2);
}

/* Set Page (0-7) and Column (0-127) on OLED */
void SSD1306_SetCursor(uint8_t page, uint8_t col){
    SSD1306_WriteCommand(0xB0 + (page & 0x07));
    SSD1306_WriteCommand(0x00 + (col & 0x0F));
    SSD1306_WriteCommand(0x10 + ((col >> 4) & 0x0F));
}

/* Clear all pixels on the OLED screen */
void SSD1306_Clear(void){
    uint8_t zero_buf[17];
    zero_buf[0] = 0x40; /* Data stream byte */
    for (int i = 1; i <= 16; i++) zero_buf[i] = 0x00;

    for (uint8_t page = 0; page < 8; page++)
    {
        SSD1306_SetCursor(page, 0);
        for (uint8_t chunk = 0; chunk < 8; chunk++)
        {
            I2C_WriteMulti(SSD1306_I2C_ADDR, zero_buf, 17);
        }
    }
}

/* Draw a single character using the mini_font table */
void SSD1306_WriteChar(char ch){
    if (ch >= 'a' && ch <= 'z') ch -= 32; /* Force uppercase */

    const uint8_t *bitmap = mini_font[0].data; /* Default to space */
    for (uint16_t i = 0; i < sizeof(mini_font)/sizeof(FontChar); i++){
        if (mini_font[i].c == ch){
            bitmap = mini_font[i].data;
            break;
        }
    }

    uint8_t buf[7];
    buf[0] = 0x40; /* Co = 0, D/C# = 1 (Data mode) */
    for (int i = 0; i < 5; i++) buf[i + 1] = bitmap[i];
    buf[6] = 0x00; /* 1-pixel spacing between characters */

    I2C_WriteMulti(SSD1306_I2C_ADDR, buf, 7);
}

/* Write a full string at a specific row (page 0-7) and column (0-127) */
void SSD1306_WriteString(uint8_t page, uint8_t col, char *str){
    SSD1306_SetCursor(page, col);
    while (*str)
    {
        SSD1306_WriteChar(*str++);
    }
}

/* Initialize SSD1306 in normal RAM mode (0xA4) and clear screen */
void SSD1306_Init(void){
    delay_ms(100);
    SSD1306_WriteCommand(0xAE);
    SSD1306_WriteCommand(0x20);
    SSD1306_WriteCommand(0x10);
    SSD1306_WriteCommand(0xB0);
    SSD1306_WriteCommand(0xC8);
    SSD1306_WriteCommand(0x00);
    SSD1306_WriteCommand(0x10);
    SSD1306_WriteCommand(0x40);
    SSD1306_WriteCommand(0x81);
    SSD1306_WriteCommand(0xFF);
    SSD1306_WriteCommand(0xA1);
    SSD1306_WriteCommand(0xA6);
    SSD1306_WriteCommand(0xA8);
    SSD1306_WriteCommand(0x3F);
    SSD1306_WriteCommand(0xA4); /* 0xA4 = Display follows RAM content */
    SSD1306_WriteCommand(0xD3);
    SSD1306_WriteCommand(0x00);
    SSD1306_WriteCommand(0xD5);
    SSD1306_WriteCommand(0xF0);
    SSD1306_WriteCommand(0xD9);
    SSD1306_WriteCommand(0x22);
    SSD1306_WriteCommand(0xDA);
    SSD1306_WriteCommand(0x12);
    SSD1306_WriteCommand(0xDB);
    SSD1306_WriteCommand(0x20);
    SSD1306_WriteCommand(0x8D);
    SSD1306_WriteCommand(0x14);
    SSD1306_Clear();            /* Wipe garbage pixels from RAM */
    SSD1306_WriteCommand(0xAF); /* Turn ON display */
}

/* Temperature condition selection (only active in AUTO mode) */
void determine_fan_mode(void){
    if (control_mode == 0){
        if (temperature < COLD_THRESH){
            fan_mode = FAN_LOW;
        }
        else if (temperature > HOT_THRESH){
            fan_mode = FAN_HIGH;
        }
        else{
            fan_mode = FAN_NORMAL;
        }
    }
}

/* Servo sweep control */
void control_servo(void){
    static uint16_t angle = SERVO_MIN_ANGLE;
    static int16_t direction = 1;
    uint32_t sweep_delay;

    if (fan_mode == FAN_STOP){
        set_servo_angle(90); /* Hold center position */
        delay_ms(20);
        return;
    }

    if (fan_mode == FAN_LOW)         sweep_delay = 10;
    else if (fan_mode == FAN_NORMAL) sweep_delay = 5;
    else                             sweep_delay = 1;

    set_servo_angle(angle);
    angle = (uint16_t)((int16_t)angle + direction);

    if (angle >= SERVO_MAX_ANGLE){
        angle = SERVO_MAX_ANGLE;
        direction = -1;
    }
    else if (angle <= SERVO_MIN_ANGLE){
        angle = SERVO_MIN_ANGLE;
        direction = 1;
    }

    delay_ms(sweep_delay);
}

/* Main program */
int main(void)
{
    char bt_msg[80];
    char oled_line[22];
    uint16_t update_counter = 50; /* Start at 50 to trigger immediate display update */

    init_GPIO();
    init_ADC1();
    init_TIM3_PWM();
    init_USART1();
    init_I2C1();
    SSD1306_Init();

    /* Draw static title on top line */
    SSD1306_WriteString(0, 10, "SMART FAN CTRL");

    while (1){
        /* 1. Always listen for incoming Bluetooth commands */
        check_bluetooth_cmd();

        /* 2. Read LM35 and update mode if in AUTO */
        temperature = read_temperature();
        determine_fan_mode();

        /* 3. Update OLED and Bluetooth every ~40 sweep steps */
        if (update_counter >= 40){
            int temp_int = (int)temperature;
            int temp_dec = (int)((temperature - (float)temp_int) * 10.0f);
            if (temp_dec < 0) temp_dec = 0;

            const char *fan_str = (fan_mode == FAN_LOW) ? "LOW " :
                                  (fan_mode == FAN_NORMAL) ? "NORM" :
                                  (fan_mode == FAN_HIGH) ? "HIGH" : "STOP";

            const char *ctrl_str = (control_mode == 0) ? "AUTO" : "MANU";

            /* Update OLED lines (Page 2, 4, 6) */
            sprintf(oled_line, "TEMP: %d.%d C   ", temp_int, temp_dec);
            SSD1306_WriteString(2, 10, oled_line);

            sprintf(oled_line, "FAN : %s      ", fan_str);
            SSD1306_WriteString(4, 10, oled_line);

            sprintf(oled_line, "MODE: %s      ", ctrl_str);
            SSD1306_WriteString(6, 10, oled_line);

            /* Send status string to Bluetooth */
            sprintf(bt_msg, "Temp: %d.%dC | Fan: %s | Mode: %s\r\n",
                    temp_int, temp_dec, fan_str, ctrl_str);
            UART_send_string(bt_msg);

            update_counter = 0;
        }
        update_counter++;

        /* 4. Step the servo smoothly */
        control_servo();
    }
}