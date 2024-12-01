#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <avr/interrupt.h>
#define F_CPU 1000000UL
// Dinh nghia cac chan
#define LCD PORTA
#define LCD_DR DDRA
#define CONT PORTB
#define CONT_DR DDRB
#define CONT_IN PINB

// Giá tri cai dat
#define RTC_ADDRESS 0x68  // Dia chi I2C cua DS1307
uint8_t volatile key_flg, key_code, hour_set_1=0, min_set_1=0, sec_set_1=0, hour_set_2=0, min_set_2=0, sec_set_2=0, hour_set_3=0, min_set_3=0, sec_set_3=0,pointer;
uint8_t a, b, c;
uint8_t buffer[7];
volatile uint8_t restart_main = 0;

// Hàm delay microsecond
void delay_us(uint16_t us) 
{
	while (us--) _delay_us(1);
}


// Gui lenh ra LCD
void OUT_LCD_CMD(uint8_t cmd)
{
	PORTB &= ~(1 << 5);  // RS = 0 (G?i l?nh)
	PORTB &= ~(1 << 6);  // RW = 0 (Ghi d? li?u)
	PORTA = cmd;         // ??a l?nh ra PORTA
	PORTB |= (1 << 7);   // Kích xung E = 1
	PORTB &= ~(1 << 7);  // Reset E = 0
	_delay_us(100);      // Ch? th?i gian t?i thi?u
}

void OUT_LCD_DATA(uint8_t data)
{
	PORTB |= (1 << 5);   // RS = 1 (G?i d? li?u)
	PORTB &= ~(1 << 6);  // RW = 0 (Ghi d? li?u)
	PORTA = data;        // ??a d? li?u ra PORTA
	PORTB |= (1 << 7);   // Kích xung E = 1
	PORTB &= ~(1 << 7);  // Reset E = 0
	_delay_us(100);      // Ch? th?i gian t?i thi?u
}


void OUT_LCD_STRING(const char* str)
{
	uint8_t i = 0; // Bi?n ??m ?? gi?i h?n s? l??ng ký t? in ra

	// Duy?t qua t?ng ký t? trong chu?i và in ra màn hình
	while (str[i] != '\0' && i < 16)
	{
		OUT_LCD_DATA(str[i]);
		i++;
	}
}

// Khoi tao LCD 8-bit
void init_lcd()
{
	LCD_DR = 0xFF;       // PORTA là output (d? li?u LCD)
	DDRB |= (1 << 5) | (1 << 6) | (1 << 7);  // PB5 (RS), PB6 (RW), PB7 (E) là output
	PORTA = 0x00;
	PORTB &= ~((1 << 5) | (1 << 6) | (1 << 7));  // ??t RS, RW, E v? 0 ban ??u
	_delay_ms(50);      // Ch? kh?i ??ng LCD

	OUT_LCD_CMD(0x38);  // Ch? ?? 8-bit, 2 dòng, font 5x7
	OUT_LCD_CMD(0x0C);  // B?t màn hình, t?t con tr?
	OUT_LCD_CMD(0x01);  // Xóa màn hình
	_delay_ms(2);       // Ch? hoàn t?t xóa
	OUT_LCD_CMD(0x06);  // T? ??ng t?ng ??a ch? con tr?
	OUT_LCD_CMD(0x80);  // ??t con tr? t?i ??u dòng 1
}


void twi_init()
{
	TWSR = 0x00;  // Prescaler = 1
	TWBR = 0x2D;  // Giá tr? t??ng ?ng v?i SCL ? 100 kHz và F_CPU = 1 MHz
	TWCR = (1 << TWEN);  // Kích ho?t TWI
}


void twi_start() 
{
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)));
}

void twi_write(uint8_t data) 
{
	TWDR = data;
	TWCR = (1 << TWINT) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT)));
}

void twi_stop() 
{
	TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
}

// Doc du lieu tu RTC qua TWI
void read_rtc(uint8_t* buffer)
{
	// G?i l?nh Start và ghi ??a ch? RTC
	twi_start();
	twi_write(RTC_ADDRESS << 1);  // Ghi ??a ch? RTC ? ch? ?? ghi
	twi_write(0x00);              // ??a ch? b?t ??u ??c (giây)
	
	// G?i l?nh Restart và chuy?n sang ch? ?? ??c
	twi_start();
	twi_write((RTC_ADDRESS << 1) | 0x01); // ??a ch? RTC ? ch? ?? ??c

	// ??c các byte d? li?u t? RTC và g?i ACK/NACK phù h?p
	for (uint8_t i = 0; i < 4; i++) {
		TWCR = (1 << TWINT) | (1 << TWEN) | (i < 3 ? (1 << TWEA) : 0); // ACK cho các byte ??u, NACK cho byte cu?i
		while (!(TWCR & (1 << TWINT))); // Ch? d? li?u s?n sàng
		buffer[i] = TWDR; // L?u d? li?u vào buffer
	}
	
	// G?i l?nh Stop ?? k?t thúc giao ti?p
	twi_stop();
}



void rtc_write()
{
	uint8_t cursor_pos = 1;  // Bien con tro tu 1 den 6
	uint8_t time_rtc[3];
	for (uint8_t i=0; i<3; i++) time_rtc[i]=0;
	uint8_t pre_time_rtc[3];

	// Hien thi "SET TIME"
	OUT_LCD_CMD(0X01);
	_delay_ms(2);
	OUT_LCD_CMD(0X0F);
	OUT_LCD_CMD(0x80);
	OUT_LCD_STRING("RTC: 00:00:00");

	// In time set tren dong thu hai
	OUT_LCD_CMD(0x85);
	OUT_LCD_DATA((time_rtc[2] / 10) + '0');
	OUT_LCD_DATA((time_rtc[2] % 10) + '0');
	OUT_LCD_DATA(':');
	OUT_LCD_DATA((time_rtc[1] / 10) + '0');
	OUT_LCD_DATA((time_rtc[1] % 10) + '0');
	OUT_LCD_DATA(':');
	OUT_LCD_DATA((time_rtc[0] / 10) + '0');
	OUT_LCD_DATA((time_rtc[0] % 10) + '0');

	// Vi tri con tro LCD ban dau
	OUT_LCD_CMD(0x85);

	while (1) {
		KEY_READ();
		if (key_code == 15)
		{  
			// Thoát ch??ng trình con n?u nh?n phím có mã 15
				uint8_t dec_to_bcd(uint8_t dec) {
					return ((dec / 10) << 4) | (dec % 10);
				}

				// Kh?i ??ng giao ti?p I2C
				twi_start();
				twi_write(RTC_ADDRESS << 1);    // G?i ??a ch? DS1307 và bit ghi (write)
				twi_write(0x00);             // G?i ??a ch? thanh ghi (Start at 0x00)

				// G?i các giá tr? th?i gian theo th? t?: giây, phút, gi?, th?, ngày, tháng, n?m
				twi_write(dec_to_bcd(time_rtc[0]));   // Giây
				twi_write(dec_to_bcd(time_rtc[1]));   // Phút
				twi_write(dec_to_bcd(time_rtc[2]));   // Gi? (12 gi? 34 phút 56 giây)
				twi_write(dec_to_bcd(3));    // Th? (Th? 3)
				twi_write(dec_to_bcd(26));   // Ngày (26)
				twi_write(dec_to_bcd(11));   // Tháng (Tháng 11)
				twi_write(dec_to_bcd(24));   // N?m (2024)

				// K?t thúc giao ti?p I2C
				twi_stop();
			break;
		}
		if (key_code == 10)
		{
			if (cursor_pos == 1)  cursor_pos = 5;  // N?u con tr? ?ang ? v? trí 1, quay l?i v? trí 6
			else if(cursor_pos == 2)
			{
				cursor_pos = 0; // N?u con tr? ?ang ? v? trí 1, quay l?i v? trí 1
				OUT_LCD_CMD(0X85);
			}
			else cursor_pos -= 2;
		}
		if (key_code == 11) ;
		if (key_code <= 9)
		{
			pre_time_rtc[2] = time_rtc[2];
			pre_time_rtc[1] = time_rtc[1];
			pre_time_rtc[0] = time_rtc[0];

			switch (cursor_pos) {
				case 1: time_rtc[2] = (time_rtc[2] % 10) + key_code * 10; break;  // Hàng chuc cua gio
				case 2: time_rtc[2] = (time_rtc[2] / 10) * 10 + key_code; break;   // Hàng don vi cua gio
				case 3: time_rtc[1] = (time_rtc[1] % 10) + key_code * 10; break;     // Hàng chuc cua phut
				case 4: time_rtc[1] = (time_rtc[1] / 10) * 10 + key_code; break;     // Hàng don vi cua phut
				case 5: time_rtc[0] = (time_rtc[0] % 10) + key_code * 10; break;     // Hàng chuc cua giay
				case 6: time_rtc[0] = (time_rtc[0] / 10) * 10 + key_code; break;     // Hàng don vi cua giay
			}

			// Kiem tra gio, phut, giay hop le
			if (time_rtc[2] > 23)
			{
				time_rtc[2] = pre_time_rtc[2];
				cursor_pos-=1;
			}
			if (time_rtc[1] > 59)
			{
				time_rtc[1] = pre_time_rtc[1];
				cursor_pos-=1;
			}
			if (time_rtc[0] > 59)
			{
				time_rtc[0] = pre_time_rtc[0];
				cursor_pos-=1;
			}

			OUT_LCD_CMD(0x85);
			OUT_LCD_DATA((time_rtc[2] / 10) + '0');
			OUT_LCD_DATA((time_rtc[2] % 10) + '0');
			OUT_LCD_DATA(':');
			OUT_LCD_DATA((time_rtc[1] / 10) + '0');
			OUT_LCD_DATA((time_rtc[1] % 10) + '0');
			OUT_LCD_DATA(':');
			OUT_LCD_DATA((time_rtc[0] / 10) + '0');
			OUT_LCD_DATA((time_rtc[0] % 10) + '0');
		}
			
		// Dat vi tri con tro theo vi tri hien thi
		switch (cursor_pos)
		{
			case 1: OUT_LCD_CMD(0x86); break;
			case 2: OUT_LCD_CMD(0x88); break;
			case 3: OUT_LCD_CMD(0x89); break;
			case 4: OUT_LCD_CMD(0x8B); break;
			case 5: OUT_LCD_CMD(0x8C); break;
			case 6: OUT_LCD_CMD(0x85); break;
		}
			
		// Chinh bien con tro, khi lon hon 6 thi ve 1, khi nho hon 1 thi qua 6
		cursor_pos = (cursor_pos % 6) + 1;
	}
}

// Chuyen tu BCD sang thap phan
uint8_t bcd_to_dec(uint8_t bcd)
{
	return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

void display_time_on_lcd(uint8_t* buffer) {
	uint8_t second = bcd_to_dec(buffer[0]);		// doc giay
	uint8_t minute = bcd_to_dec(buffer[1]);		// doc phut
	uint8_t hour = bcd_to_dec(buffer[2]);		// doc gio
	uint8_t wd = bcd_to_dec(buffer[3]);			// doc thu
	uint8_t day = bcd_to_dec(buffer[4]);		// doc ngay
	uint8_t month = bcd_to_dec(buffer[5]);		// doc thang
	uint8_t year = bcd_to_dec(buffer[6]);		// doc nam

	OUT_LCD_CMD(0X01);
	OUT_LCD_CMD(0x80);
	OUT_LCD_STRING(" TIME: ");
	
	OUT_LCD_DATA((hour / 10) + '0');
	OUT_LCD_DATA((hour % 10) + '0');
	OUT_LCD_DATA(':');
	OUT_LCD_DATA((minute / 10) + '0');
	OUT_LCD_DATA((minute % 10) + '0');
	OUT_LCD_DATA(':');
	OUT_LCD_DATA((second / 10) + '0');
	OUT_LCD_DATA((second % 10) + '0');
}


void GET_KEY16()
{
	uint8_t col, row, buff, j;
	key_flg = 0;
	col = 0xEF; // Bat dau quet tu cot dau tien (PD4 - PD7)
	j = 0;      // S? cot, bat dau tu 0

	while ((key_flg == 0) && (j < 4))  // Duyet qua cac cot
	{
		PORTD = (PORTD & 0x0F) | col;  // Dat cot tu PD4-PD7, giu nguyen PD0-PD3
		buff = col;
		row = PIND & 0x0F;  // Doc hang tu PD0 - PD3

		if (row != 0x0F)  // Kiem tra co phim duoc nhan
		{
			// Xac dinh tung hang bang cach dich tung hang qua phai
			if (row == 0x0E) key_code = j + 0;    // row 0
			else if (row == 0x0D) key_code = j + 4; // row 1
			else if (row == 0x0B) key_code = j + 8; // row 2
			else if (row == 0x07) key_code = j + 12; // row 3
			key_flg = 1;  // Danh dau da doc phim
		}
		else
		{
			// Dich sang cot tiep theo
			col = (buff << 1) | 0x01;  // Chuyen cot ke tiep, giu bit thap
			j++;  // Tang so cot
		}
	}
}

void KEY_READ()  // Doc phim va tra ve ma phim chinh xac (chong rung)
{
	uint8_t tam, i;
	for (i = 50; i >= 1; i--)
	{
		GET_KEY16();
		if (key_flg == 0) i = 50;  // Neu chua co phim, doc lai tu dau
	}
	tam = key_code;
	for (i = 50; i >= 1; i--)
	{
		GET_KEY16();
		if (key_flg == 1) i = 50;  // N?u co phim, doc lai tu dau
	}
	key_code = tam;
}


void SET_TIME_1()
{
	uint8_t cursor_pos = 1;  // Bien con tro tu 1 den 6

	// Hien thi "SET TIME"
	OUT_LCD_CMD(0X01);
	_delay_ms(2);
	OUT_LCD_CMD(0X0F);
	OUT_LCD_CMD(0x80);
	OUT_LCD_STRING("SET TIME 1:");

	// In time set tren dong thu hai
	OUT_LCD_CMD(0xC0);
	OUT_LCD_DATA((hour_set_1 / 10) + '0');
	OUT_LCD_DATA((hour_set_1 % 10) + '0');
	OUT_LCD_DATA(':');
	OUT_LCD_DATA((min_set_1 / 10) + '0');
	OUT_LCD_DATA((min_set_1 % 10) + '0');
	OUT_LCD_DATA(':');
	OUT_LCD_DATA((sec_set_1 / 10) + '0');
	OUT_LCD_DATA((sec_set_1 % 10) + '0');

	// Vi tri con tro LCD ban dau
	OUT_LCD_CMD(0xC0);

	while (1) {
		KEY_READ();
		if (key_code == 12)
		{  // Thoát ch??ng trình con n?u nh?n phím có mã 13
			break;
		}
		if (key_code == 10) 
		{
			if (cursor_pos == 1)  cursor_pos = 5;  // N?u con tr? ?ang ? v? trí 1, quay l?i v? trí 6
			else if(cursor_pos == 2) 
			{
				cursor_pos = 0; // N?u con tr? ?ang ? v? trí 1, quay l?i v? trí 1
				OUT_LCD_CMD(0XC0);
			}
			else cursor_pos -= 2;
		}
		if (key_code == 11) ;
		if (key_code <= 9)
		{ 
			uint8_t prev_hour_set_1 = hour_set_1;
			uint8_t prev_min_set_1 = min_set_1;
			uint8_t prev_sec_set_1 = sec_set_1;

			switch (cursor_pos) {
				case 1: hour_set_1 = (hour_set_1 % 10) + key_code * 10; break;  // Hàng chuc cua gio
				case 2: hour_set_1 = (hour_set_1 / 10) * 10 + key_code; break;   // Hàng don vi cua gio
				case 3: min_set_1 = (min_set_1 % 10) + key_code * 10; break;     // Hàng chuc cua phut
				case 4: min_set_1 = (min_set_1 / 10) * 10 + key_code; break;     // Hàng don vi cua phut
				case 5: sec_set_1 = (sec_set_1 % 10) + key_code * 10; break;     // Hàng chuc cua giay
				case 6: sec_set_1 = (sec_set_1 / 10) * 10 + key_code; break;     // Hàng don vi cua giay
			}

			// Kiem tra gio, phut, giay hop le
			if (hour_set_1 > 23)
			{
				hour_set_1 = prev_hour_set_1;
				cursor_pos-=1;
			}
			if (min_set_1 > 59)
			{
				min_set_1 = prev_min_set_1;
				cursor_pos-=1;
			}
			if (sec_set_1 > 59)
			{
				sec_set_1 = prev_sec_set_1;
				cursor_pos-=1;
			}

			OUT_LCD_CMD(0xC0);
			OUT_LCD_DATA((hour_set_1 / 10) + '0');
			OUT_LCD_DATA((hour_set_1 % 10) + '0');
			OUT_LCD_DATA(':');
			OUT_LCD_DATA((min_set_1 / 10) + '0');
			OUT_LCD_DATA((min_set_1 % 10) + '0');
			OUT_LCD_DATA(':');
			OUT_LCD_DATA((sec_set_1 / 10) + '0');
			OUT_LCD_DATA((sec_set_1 % 10) + '0');
		}
		
		// Dat vi tri con tro theo vi tri hien thi
		switch (cursor_pos)
		{
			case 1: OUT_LCD_CMD(0xC1); break;
			case 2: OUT_LCD_CMD(0xC3); break;
			case 3: OUT_LCD_CMD(0xC4); break;
			case 4: OUT_LCD_CMD(0xC6); break;
			case 5: OUT_LCD_CMD(0xC7); break;
			case 6: OUT_LCD_CMD(0xC0); break;
		}
		
		// Chinh bien con tro, khi lon hon 6 thi ve 1, khi nho hon 1 thi qua 6
		cursor_pos = (cursor_pos % 6) + 1;
	}
}

void SET_TIME_2()
{
	uint8_t cursor_pos = 1;  // Bien con tro tu 1 den 6

	// Hien thi "SET TIME"
	OUT_LCD_CMD(0X01);
	_delay_ms(2);
	OUT_LCD_CMD(0X0F);
	OUT_LCD_CMD(0x80);
	OUT_LCD_STRING("SET TIME 2:");

	// In time set tren dong thu hai
	OUT_LCD_CMD(0xC0);
	OUT_LCD_DATA((hour_set_2 / 10) + '0');
	OUT_LCD_DATA((hour_set_2 % 10) + '0');
	OUT_LCD_DATA(':');
	OUT_LCD_DATA((min_set_2 / 10) + '0');
	OUT_LCD_DATA((min_set_2 % 10) + '0');
	OUT_LCD_DATA(':');
	OUT_LCD_DATA((sec_set_2 / 10) + '0');
	OUT_LCD_DATA((sec_set_2 % 10) + '0');

	// Vi tri con tro LCD ban dau
	OUT_LCD_CMD(0xC0);

	while (1) {
		KEY_READ();
		if (key_code == 13)	break;
		if (key_code == 10) 
		{
			if (cursor_pos == 1)  cursor_pos = 5;  // N?u con tr? ?ang ? v? trí 1, quay l?i v? trí 6
			else if(cursor_pos == 2)
			{
				cursor_pos = 0; // N?u con tr? ?ang ? v? trí 1, quay l?i v? trí 1
				OUT_LCD_CMD(0XC0);
			}
			else cursor_pos -= 2;
		}
		if (key_code == 11) ;
		if (key_code <= 9)
		{  // Ch? x? lý các phím có giá tr? t? 0 ??n 9
			uint8_t prev_hour_set_2 = hour_set_2;
			uint8_t prev_min_set_2 = min_set_2;
			uint8_t prev_sec_set_2 = sec_set_2;

			switch (cursor_pos) {
				case 1: hour_set_2 = (hour_set_2 % 10) + key_code * 10; break;  // Hàng ch?c c?a gi?
				case 2: hour_set_2 = (hour_set_2 / 10) * 10 + key_code; break;   // Hàng ??n v? c?a gi?
				case 3: min_set_2 = (min_set_2 % 10) + key_code * 10; break;     // Hàng ch?c c?a phút
				case 4: min_set_2 = (min_set_2 / 10) * 10 + key_code; break;     // Hàng ??n v? c?a phút
				case 5: sec_set_2 = (sec_set_2 % 10) + key_code * 10; break;     // Hàng ch?c c?a giây
				case 6: sec_set_2 = (sec_set_2 / 10) * 10 + key_code; break;     // Hàng ??n v? c?a giây
			}

			// Kiem tra gio, phut, giay hop le
			if (hour_set_2 > 23)
			{
				hour_set_2 = prev_hour_set_2;
				cursor_pos-=1;
			}
			if (min_set_2 > 59)
			{
				min_set_2 = prev_min_set_2;
				cursor_pos-=1;
			}
			if (sec_set_2 > 59)
			{
				sec_set_2 = prev_sec_set_2;
				cursor_pos-=1;
			}

			OUT_LCD_CMD(0xC0);
			OUT_LCD_DATA((hour_set_2 / 10) + '0');
			OUT_LCD_DATA((hour_set_2 % 10) + '0');
			OUT_LCD_DATA(':');
			OUT_LCD_DATA((min_set_2 / 10) + '0');
			OUT_LCD_DATA((min_set_2 % 10) + '0');
			OUT_LCD_DATA(':');
			OUT_LCD_DATA((sec_set_2 / 10) + '0');
			OUT_LCD_DATA((sec_set_2 % 10) + '0');
		}
		
		// Dat vi tri con tro theo vi tri hien thi
		switch (cursor_pos)
		{
			case 1: OUT_LCD_CMD(0xC1); break;
			case 2: OUT_LCD_CMD(0xC3); break;
			case 3: OUT_LCD_CMD(0xC4); break;
			case 4: OUT_LCD_CMD(0xC6); break;
			case 5: OUT_LCD_CMD(0xC7); break;
			case 6: OUT_LCD_CMD(0xC0); break;
		}
		
		// Chinh bien con tro, khi lon hon 6 thi ve 1, khi nho hon 1 thi qua 6
		cursor_pos = (cursor_pos % 6) + 1;
	}
}

void SET_TIME_3()
{
	uint8_t cursor_pos = 1;  // Bien con tro tu 1 den 6

	OUT_LCD_CMD(0X01);
	_delay_ms(2);
	OUT_LCD_CMD(0X0F);
	OUT_LCD_CMD(0x80);
	OUT_LCD_STRING("SET TIME 3:");

	// In time set tren dong thu hai
	OUT_LCD_CMD(0xC0);
	OUT_LCD_DATA((hour_set_3 / 10) + '0');
	OUT_LCD_DATA((hour_set_3 % 10) + '0');
	OUT_LCD_DATA(':');
	OUT_LCD_DATA((min_set_3 / 10) + '0');
	OUT_LCD_DATA((min_set_3 % 10) + '0');
	OUT_LCD_DATA(':');
	OUT_LCD_DATA((sec_set_3 / 10) + '0');
	OUT_LCD_DATA((sec_set_3 % 10) + '0');

	// Vi tri con tro LCD ban dau
	OUT_LCD_CMD(0xC0);

	while (1) {
		KEY_READ();
		if (key_code == 14)
		{  // Thoát ch??ng trình con n?u nh?n phím có mã 12
			break;
		}
		if (key_code == 10)
		{
			if (cursor_pos == 1)  cursor_pos = 5;  // N?u con tr? ?ang ? v? trí 1, quay l?i v? trí 6
			else if(cursor_pos == 2)
			{
				cursor_pos = 0; // N?u con tr? ?ang ? v? trí 1, quay l?i v? trí 1
				OUT_LCD_CMD(0XC0);
			}
			else cursor_pos -= 2;
		}
		if (key_code == 11) ;
		if (key_code <= 9)
		{  // Ch? x? lý các phím có giá tr? t? 0 ??n 9
			uint8_t prev_hour_set_3 = hour_set_3;
			uint8_t prev_min_set_3 = min_set_3;
			uint8_t prev_sec_set_3 = sec_set_3;

			switch (cursor_pos) {
				case 1: hour_set_3 = (hour_set_3 % 10) + key_code * 10; break;  // Hàng ch?c c?a gi?
				case 2: hour_set_3 = (hour_set_3 / 10) * 10 + key_code; break;   // Hàng ??n v? c?a gi?
				case 3: min_set_3 = (min_set_3 % 10) + key_code * 10; break;     // Hàng ch?c c?a phút
				case 4: min_set_3 = (min_set_3 / 10) * 10 + key_code; break;     // Hàng ??n v? c?a phút
				case 5: sec_set_3 = (sec_set_3 % 10) + key_code * 10; break;     // Hàng ch?c c?a giây
				case 6: sec_set_3= (sec_set_3 / 10) * 10 + key_code; break;     // Hàng ??n v? c?a giây
			}

			// Kiem tra gio, phut, giay hop le
			if (hour_set_3 > 23)
			{
				hour_set_3 = prev_hour_set_3;
				cursor_pos-=1;
			}
			if (min_set_1 > 59)
			{
				min_set_3 = prev_min_set_3;
				cursor_pos-=1;
			}
			if (sec_set_3 > 59)
			{
				sec_set_3 = prev_sec_set_3;
				cursor_pos-=1;
			}

			OUT_LCD_CMD(0xC0);
			OUT_LCD_DATA((hour_set_3 / 10) + '0');
			OUT_LCD_DATA((hour_set_3 % 10) + '0');
			OUT_LCD_DATA(':');
			OUT_LCD_DATA((min_set_3 / 10) + '0');
			OUT_LCD_DATA((min_set_3 % 10) + '0');
			OUT_LCD_DATA(':');
			OUT_LCD_DATA((sec_set_3 / 10) + '0');
			OUT_LCD_DATA((sec_set_3 % 10) + '0');
		}
		
		// Dat vi tri con tro theo vi tri hien thi
		switch (cursor_pos)
		{
			case 1: OUT_LCD_CMD(0xC1); break;
			case 2: OUT_LCD_CMD(0xC3); break;
			case 3: OUT_LCD_CMD(0xC4); break;
			case 4: OUT_LCD_CMD(0xC6); break;
			case 5: OUT_LCD_CMD(0xC7); break;
			case 6: OUT_LCD_CMD(0xC0); break;
		}
		
		// Chinh bien con tro, khi lon hon 6 thi ve 1, khi nho hon 1 thi qua 6
		cursor_pos = (cursor_pos % 6) + 1;
	}
}


void servo(void)
{
	PORTC |= (1 << PC2);	// Set PC2 high
	PORTC |= (1<<PC3);
	_delay_us(1500);
	PORTC &= ~(1 << PC2);
	_delay_us(18500);
	_delay_ms(1000);
	PORTC |= (1 << PC2); // Set PC2 high
	_delay_us(1000);
	PORTC &= ~(1<< PC2);
	_delay_us(19000);
	PORTC &= ~(1<< PC3);
}

ISR(INT1_vect)
{
loop:	OUT_LCD_CMD(0X01);
		_delay_ms(2);
		OUT_LCD_CMD(0X0F);
		OUT_LCD_CMD(0x80);
		OUT_LCD_STRING("  CONTROL MODE  ");
		while (1)
	{
		_delay_ms(500);
		KEY_READ();
		
		if (key_code == 10)
		{
			rtc_write();
			key_code=0;
			goto loop;
		}
		else if (key_code == 11)
		{
			servo();
			goto loop;
		}
		else if (key_code == 12) {
			SET_TIME_1();
			goto loop;
		}
		else if (key_code == 13) {
			SET_TIME_2();
			goto loop;
		}
		else if (key_code == 14) {
			SET_TIME_3();
			goto loop;
		}
		if (key_code == 15)	{
			key_code=0;
			return;		// Thooat chuong trinh con
		}
			
	}
}


int main(void) 
{

	DDRB |= (1<<5)|(1<<6)|(1<<7);
	DDRC |= (1<<2)|(1<<3); 
	DDRD = 0xF0;
	PORTD = 0xFF;							// LED ma tran
	MCUCR &= (0 << ISC11) & (0 << ISC10);	// ISC11 = 1, ISC10 = 1: Ngat ngoai
	MCUCSR |= (1 << ISC2);
	GICR |= (1 << INT2)|(1 << INT1);					// Cho phep ngat ngoai INT1
	sei();									// Cho phep ngat toan cuc

	init_lcd();								// Khoi dong LCD va TWI
	twi_init();

	
	OUT_LCD_CMD(0X0C);
	read_rtc(buffer);
	display_time_on_lcd(buffer);
	_delay_ms(2);
	GET_KEY16();
	while(1)
	{
		OUT_LCD_CMD(0X0C);
		read_rtc(buffer);
		display_time_on_lcd(buffer);
		uint8_t second = bcd_to_dec(buffer[0]); 
		uint8_t minute = bcd_to_dec(buffer[1]);
		uint8_t hour = bcd_to_dec(buffer[2]);
		if ((hour == hour_set_1 && minute == min_set_1 && second == sec_set_1) | (hour == hour_set_2 && minute == min_set_2 && second == sec_set_2) | (hour == hour_set_3 && minute == min_set_3 && second == sec_set_3))   
		{ 
			servo();
		} 
		
		else { 
			PORTC &= ~(1 << 2); 
		}
		_delay_ms(100);
		}
	}
