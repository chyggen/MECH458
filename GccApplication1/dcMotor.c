/*
 * dc_motor.c
 *
 * Created: 2023-10-25 3:57:22 PM
 *  Author: mech458
 */ 

#include <avr/io.h> // the header of I/O port

#include "dcMotor.h"
#include "utils.h"

motorDir_t current_dir; // global to store direction

void pwmInit()
{
	  // Configure TCCR0A and TCCR0B registers for fast PWM mode with TOP = 0xFF (255)
	  TCCR0A = (1 << WGM01) | (1 << WGM00);

	  //// Enable timer overflow interrupt on MAX
	  //TIMSK0 = (1 << TOIE0);

	  //// Enable output compare interrupt for Timer 0 (pb7 - pin 13 on pwm)
	  //TIMSK0 |= (1 << OCIE0A);

	  // Set compare match output mode to clear on match
	  TCCR0A |= (1 << COM0A1);

	  // Set prescaler to 8
	  TCCR0B |= (1 << CS01); // datasheet: f0Cnx = fclk / (N*256), where N = prescale value of 1,8,64,256, or 1024)

}

void pwmSet(uint8_t compareVal)
{
	// Set the value of Output Compare Register A (OCRA) for the provided duty cycle
	OCR0A = compareVal; 
}

void motorJog(motorDir_t dir, uint8_t compareVal) //direction (ccw - fwd)
{
	
	//// break 5ms if were changing direction
	//motor_dir_t prevDir = invalid;
	//
	////portB 7XXX 3210, where bits [0123 XXX7] -> [pwm,X,X,X,Ib,Eb,Ea,Ia]
	//
	//if(prevDir != dir){
		//motor_brake();
		//mTimer(5);//delay 5ms if changing direction to let switches not float
		//
	//}
	
	pwmSet(compareVal);
	//if (dir == forward)
	//{

		PORTB = (PORTB & 0b11110000) | 0b00001110; //INb HI and enables HI

	//}
	//else if (dir == reverse)
	//{
		//PORTB = (PORTB & 0b11110000) | 0b00000111; //INa HI and enables HI
	//}
	//
	//prevDir = dir; // to see if we are changing directions
	
}

void motorBrake() //break HI
{
	PORTB = (PORTB & 0b11110000) | 0b00001111;	//INa/INb = 1/1 enables HI
}

