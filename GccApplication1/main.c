/* ##################################################################
# PROJECT: MECH 458 Final Project
# GROUP: 3
# NAME 1: Anish Sivakumar, V00940537
# NAME 2: Christan Hyggen, V00945257
# DESC: Inspection system to classify objects and sort into a bucket
# REVISED ############################################################### */

#include <util/delay_basic.h>
#include <avr/interrupt.h>
#include <stdlib.h> // the header of the general-purpose standard library of C programming language
#include <stdio.h> 
#include <avr/io.h> // the header of I/O port

// Custom includes
#include "utils.h"
#include "stepper.h"
#include "dcMotor.h"
#include "lcd.h"
#include "linkedQueue.h"


// Calibration mode enable switch, uncomment to calibrate system
//#define CALIBRATION_MODE

// Stepper test mode 
//#define STEPPER_TEST

#ifdef STEPPER_TEST
#include "tests.h"
#endif

// Object detection threshold value
#define OBJECT_THRESH 1023

typedef enum FSM_state
{
	POLLING,
	STEPPER_CONTROL,
	PAUSE,
	END
}FSM_state_t;

// Main FSM state variable
FSM_state_t state = POLLING;

// Motor settings
volatile motorDir_t motorDir;
volatile uint8_t motorPwm = 0xA0; //0x90 before

// Stepper settings
int stepperLastPos = 0;

// ADC classification variables
uint16_t adcVal = OBJECT_THRESH;
uint16_t adcMin = OBJECT_THRESH;
uint16_t adcReadings = 0;
int objDetect = 0;
cyl_t cylType = DISCARD;

// Belt end detect flag
volatile int edFlag = 0;

// Hall effect flag
volatile int heFlag = 0;

// Rampdown flag
volatile int rdFlag = 0;

// Pause flag
volatile int pFlag = 0;

// Drop timer flag
volatile int dFlag = 1;

// OR sensor falling edge flag
volatile int orFlag = 0;

//TESTING
volatile int edCount = 0;

/* ################## MAIN ROUTINE ################## */

int main(int argc, char *argv[]){
	
	// Pin config
	DDRA = 0b11111111; // PORTA = output
	DDRB = 0b11111111; // PORTB = output
	DDRC = 0b11111111; // PORTC = output
	DDRD = 0b00000000; // PORTD = input
	DDRE = 0b00000000; // PORTE = input
	DDRL = 0b11111111; // PORTL = output

	// Set initial outputs
	PORTA = 0x00;
	PORTB = 0x00;
	PORTC = 0x00; 
	
		
	// slow clock to 8MHz
	CLKPR = 0x80;
	CLKPR = 0x01; 
	
	// Disable all interrupts
	cli();
	
	// Perform component initialization while interrupts are disabled	
	timerInit();
	pwmInit(); 
	adcInit(); 
	eiInit();
	// Re-enable all interrupts
	sei();		
	
	// Set up LCD
	InitLCD(LS_BLINK|LS_ULINE);
	LCDClear();
	
	// Set defaults
	motorDir = forward;
	
	// Object queue variables
	link *qHead, *qTail;
	link *newLink, *poppedLink;
	cyl_t processedCount[4] = {};
	lqSetup(&qHead, &qTail);	
	
	// calibrate the stepper
	while(heFlag == 0)
	{
		rotate(1,0);
	}
	rotate(4,1);
	heFlag = 0;
	resetPosition();
	
	// testing routines
	#ifdef STEPPER_TEST
	stepperContinueTest();
	#endif 
		
	///////////////////////////
	// Main FSM control loop //
	///////////////////////////

	
	while(1)
	{
		switch (state)
		{
			case POLLING:
				/* 
					In this state we perform the following tasks:
					- The reflection sensor is polled at a constant rate
					- Objects are added and removed from the object queue
					- The DC motor is run at max speed
					- The stepper motor can be run at a constant speed 
					
					Exit conditions:
					If the beam sensor is triggered and the plate is not in the correct position:
						-- Compute stepper command
						-> Go to STEPPER_CONTROL
					If the pause button is pressed:
						-> Go to PAUSE
					If the object queue is empty and the rampdown flag has been set:
						-> Go to END	
				*/
				motorJog(forward,motorPwm);
				
				if (orFlag)
				{
					// Object has finished passing through, process it
					#ifdef CALIBRATION_MODE
						displayCalibration(adcMin, adcReadings);
						
					#else
						cylType = getCylType(adcMin);
						
						if (cylType != DISCARD && adcReadings > 4)
						{
							LCDWriteIntXY(6,1,adcMin,4)
							LCDWriteIntXY(11,1,adcReadings,3);
							
							initLink(&newLink);
							newLink->e.itemCode = cylType;
							lqPush(&qHead, &qTail, &newLink);

						}
					#endif
					
					// Reset values
					objDetect = 0;
					adcMin = 0xFFFF;
					adcReadings = 0;
					orFlag = 0;
				}
				
				// Get ADC reading
				adcVal = adcRead();
				
				// Object processing logic
				if  (PINE & 0x10)  
				{
					// Were detecting an object
					objDetect = 1;
					if (adcVal < adcMin)
					{
						adcMin = adcVal;
					}
					adcReadings++;
					
				}
				
				// Check rampdown flag
				if (rdFlag)
				{
					LCDWriteStringXY(12,0,"RDWN");
					if (lqIsEmpty(&qHead))
					{
						// Wait a bit to let the last piece fall
						mTimer(300);
						state = END;
					}
				}
				
				// Check pause flag
				if (pFlag)
				{
					state = PAUSE;
				}
				
				#ifndef CALIBRATION_MODE
				// Check end of belt flag
				if (edFlag)
				{
					
					state = STEPPER_CONTROL;
					motorBrake();
					continue;
				}
				
				// Print some debug stuff 
				LCDWriteIntXY(8,0,lqSize(&qHead,&qTail),2);
				
				if (qHead != NULL)
				{
					LCDWriteIntXY(0,0,qHead->e.itemCode,1);
				}
				else
				{
					LCDWriteIntXY(0,0,4,1);
				}
				#endif
				
				break;
			
			case STEPPER_CONTROL:
				/* 
					In this state we only focus on controlling the stepper 
					as efficiently as possible. 
					
					Exit conditions:
					If the stepper is in position to drop the next object:
						-> Go to POLLING
					If the pause button is pressed:
						-> Go to PAUSE
				*/
				LCDWriteIntXY(0,1,edCount,2);
				lqPop(&qHead, &qTail, &poppedLink);
				if (poppedLink == NULL)
				{
					LCDWriteIntXY(3,1,999,3);
				}
				
				// Wait if dFlag not set (drop timer to finish)
				while(dFlag == 0); 
				
				dFlag = smartAlign(poppedLink->e.itemCode, &qHead, &qTail); 

				if (dFlag == 0){ //if we are rotating
					PORTL = 0b11111111;
					// Enable Timer 4 
					TCNT4 = 0;
					TCCR4B |= (1 << CS42);
					
				}
				
				if (poppedLink != NULL)
				{
					processedCount[poppedLink->e.itemCode]++;
				}

				free(poppedLink);
				motorJog(motorDir, motorPwm);
				edFlag = 0;

				state = POLLING;
				break;
				
			case PAUSE:
				/*
					Upon entering this state, both motors are stopped and 
					the previous state is saved. We exit if the pause button 
					is pressed again
					
					Exit conditions:
					If the pause button is pressed and we entered from POLLING:
						-> Go to POLLING
					If the pause button is pressed and we entered from STEPPER_CONTROL:
						-> Go to STEPPER_CONTROL
				*/
				// Disable all interrupts
			
			
				motorBrake();
				LCDClear();
				LCDWriteStringXY(0,0,"BL WH AL ST US");
				LCDWriteIntXY(0 ,1,processedCount[BLACK],2);
				LCDWriteIntXY(3 ,1,processedCount[WHITE],2);
				LCDWriteIntXY(6 ,1,processedCount[ALUM ],2);
				LCDWriteIntXY(9 ,1,processedCount[STEEL],2);
				LCDWriteIntXY(12,1,lqSize(&qHead,&qTail), 2);
				
				while (pFlag);
						
				LCDClear();
				state = POLLING;
				motorJog(motorDir, motorPwm);

				break;
			
			case END:
				/*
					In this state, motors are turned off and the program terminates.
				*/
				motorBrake();
				LCDClear();
				LCDWriteStringXY(0,0,"BL: WH: AL: ST:");
				LCDWriteIntXY(0 ,1,processedCount[BLACK],3);
				LCDWriteIntXY(4 ,1,processedCount[WHITE],3);
				LCDWriteIntXY(8 ,1,processedCount[ALUM ],3);
				LCDWriteIntXY(12,1,processedCount[STEEL],3);
				return (0); 	
		}
	}
	return (0); 
}

//  switch 0 - rampdown
ISR(INT0_vect)
{ 
	mTimer(15);
	
	if ((PIND & 0x01) == 0){
			
		while((PIND & 0x01) == 0);
		mTimer(15);
		
		// Initialize the counter value to 0
		TCNT5 = 0;
		TCCR5B |= (1 << CS50) | (1 << CS52); //enable timer

	}
	
}

//  switch 1 - pause
ISR(INT1_vect)
{ 
	mTimer(15);
	
	if ((PIND & 0x02) == 0){
		pFlag = !pFlag;
		
		while((PIND & 0x02) == 0);
		mTimer(15);
	}
	
	
}

// End of Belt Detect
ISR(INT2_vect)
{
	//mTimer(1);
	//if (!(PIND & 0x04) )
	//{
		if (!edFlag)
		{
			edCount++;
		}
		edFlag = 1;
	//}

	
}

// Hall-Effect Sensor for stepper calibration
ISR(INT3_vect)
{
	heFlag = 1;
}

ISR(INT4_vect)
{
	orFlag = 1;
}

int on = 1;

// Timer 4 overflow interrupt service routine sets the drop flag
ISR(TIMER4_COMPA_vect) {
	dFlag = 1;
	TCCR4B &= ~(1 << CS42); //disable timer
	PORTL = 0b00000000;
}

// Timer 5 overflow interrupt service routine
ISR(TIMER5_COMPA_vect) {
	rdFlag = 1;
	TCCR5B &= ~((1 << CS50) | (1 << CS52)); //disable
}