#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "images.h"
#include "snakeGame_definition.h"
#include "snakeGame_structures.h"

/* Function Declarations */

void set_A9_IRQ_stack(void);
void enable_A9_interrupts(void);
void config_GIC(void);
void config_interrupt(int N, int CPU_target);
void config_KEYS(void);
void config_PS2(void);
void pushbutton_ISR(void);
void PS2_ISR(void);

void up();
void down();
void left();
void right();

void finalScore(int curScore);
void draw_score();
void reset();
bool two_lines_intersect(int x1_start, int y1_start, int x1_end, int y1_end, 
						 int x2_start, int y2_start, int x2_end, int y2_end);
bool lines_overlap(Snake *list);
void clear_screen();
void draw_line(int x0, int y0, int x1, int y1, short int colour);
void swap (int* x, int* y);
void plot_pixel(int x, int y, short int line_color);
void wait_for_vsync();
void draw_image(int initial_x, int initial_y, int imageArray[], unsigned width, unsigned height);
void draw_borders();

Node *createNode(int x_val, int y_val);
bool insertAtFront(Snake *list, int x_val, int y_val);
bool insertAtBack(Snake *list, int x_val, int y_val);
void deleteBack(Snake *list);
void deleteSnake(Node *head);
bool copyList(Snake *listOrg, Snake *listCopy);


/* Global Variable Declarations */

volatile int pixel_buffer_start; // global variable
volatile int * pixel_ctrl_ptr = (int *)0xFF203020;

char byte1 = 0, byte2 = 0, byte3 = 0;
char seg7[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
0x7f, 0x67, 0x77};

volatile int prevDir = 3;
volatile int curDir = 3;		// 0 = up, 1 = down, 2 = left, 3 = right
volatile Snake prev2, prev, cur;

volatile bool run = false;
volatile bool newGame = true;
volatile bool instructions_cleared = false;
volatile bool gameOver = false;
volatile bool mode_selected = false;
volatile bool gameOver_drawn = false;
volatile bool overlap = false;

volatile int count = 0;
volatile int score = 0;
volatile int score_ones, score_tenth;
volatile int mode = DEFAULT;

volatile int * KEY_ptr 		= (int *)0xFF200050;
volatile int * HEX3_0_ptr 	= (int *)0xFF200020;
volatile int * LEDR_ptr 	= (int *)0xFF200000;	// red LED address
volatile int * SW_ptr		= (int *)0xFF200040;	// SW slide switch address

	
/* MAIN */

int main(void) {
	
	// configure GIC for interrupts 
	set_A9_IRQ_stack();
	config_GIC();
	config_KEYS();
	config_PS2();
	enable_A9_interrupts();
	
	// declare other variables
	int colours[10] = {WHITE, YELLOW, RED, GREEN, BLUE, CYAN, 
				   		MAGENTA, GREY, PINK, ORANGE};
	int fruitColour[NUM_FRUITS];
	int x_fruitCur[NUM_FRUITS], y_fruitCur[NUM_FRUITS];
	int x_fruitPrev[NUM_FRUITS], y_fruitPrev[NUM_FRUITS];
	int x_fruitPrev2[NUM_FRUITS], y_fruitPrev2[NUM_FRUITS];

	
	// initialize locations and colours of fruits
	int i = 0;
	for (; i < NUM_FRUITS; i++) {
		// generate random colour (b/w 0 and 9)
		fruitColour[i] = colours[rand() % 10];
		// generate random position for fruit
		x_fruitCur[i] = rand() % (RESOLUTION_X - 5);
		y_fruitCur[i] = rand() % (RESOLUTION_Y - 5);
		
		// initiliaze positions
		x_fruitPrev[i] = x_fruitCur[i];
		y_fruitPrev[i] = y_fruitCur[i];
		x_fruitPrev2[i] = x_fruitPrev[i];
		y_fruitPrev2[i] = y_fruitPrev[i];
	}
    
	
	// initialize starting position of snake to be centre of screen
	int x_val = (0 + RESOLUTION_X)/2;
	int y_val = (0 + RESOLUTION_Y)/2;

	cur.head = NULL;
	prev.head = NULL;
	prev2.head = NULL;
	
	i = 0;
	for (; i < 100; i+=5) {
		insertAtBack(&cur, x_val-i, y_val);
	}
	
	/* set front pixel buffer to start of FPGA On-chip memory */
    *(pixel_ctrl_ptr + 1) = 0xC8000000; // first store the address in the 
                                        // back buffer
    
	/* now, swap the front/back buffers, to set the front buffer location */
    wait_for_vsync();
    
	/* initialize a pointer to the pixel buffer, used by drawing functions */
    pixel_buffer_start = *pixel_ctrl_ptr;
    clear_screen(); // pixel_buffer_start points to the pixel buffer
    
	/* set back pixel buffer to start of SDRAM memory */
    *(pixel_ctrl_ptr + 1) = 0xC0000000;
    pixel_buffer_start = *(pixel_ctrl_ptr + 1); // we draw on the back buffer
    clear_screen(); // pixel_buffer_start points to the pixel buffer
    
	unsigned SW_value;
	
	while (1) {

		SW_value = (unsigned int) *SW_ptr;
		
		// if SW[0] is on, reset the game
		if (SW_value == 0b0000000001) {
			reset();
		}
		

		if (newGame) {
			
			// draw cover image of game
			draw_image(0, 0, snakeGame, 320, 240);
			wait_for_vsync();
    		pixel_buffer_start = *(pixel_ctrl_ptr + 1);
			draw_image(0, 0, snakeGame, 320, 240);
			
			// wait for player to select game mode: SW[3] = DEFULT, SW[4] = EASY, SW[6] = HARD
			while (!mode_selected) {
				SW_value = (unsigned int) *SW_ptr;

				if ((SW_value == 0b0000001000) || (SW_value == 0b0000010000) || (SW_value == 0b0000100000)) {
					mode_selected = true;
					if (SW_value == 0b0000001000) {
						mode = DEFAULT;
					}
					else if (SW_value == 0b0000010000) {
						mode = EASY;
					}
					else if (SW_value == 0b0000100000) {
						mode = HARD;
					}
					*(LEDR_ptr) = SW_value;
				}
			}
			
			
			// wait for player to turn on the run signal: SW[1]
			while (!run) {
				SW_value = (unsigned int) *SW_ptr;
				if (SW_value == 0b0000000010){	
					*(LEDR_ptr) = SW_value;
					run = true;
				}
			}
			
			// clear the cover image
    		clear_screen();
			wait_for_vsync();
    		pixel_buffer_start = *(pixel_ctrl_ptr + 1);
    		clear_screen();
			
			newGame = false;
		}
		
		if (!gameOver) {
			draw_borders();
		}

		if (!gameOver && (count == mode)) {
			count = 0;			
			
			/* ERASE PREVIOUS 2 LOCATIONS */
			
			// erase locations in prev2
			Node *prev2Ptr = prev2.head;
			if (prev2Ptr != NULL) {
				while (prev2Ptr != NULL) {
					i = 0;
					for (; i <= 5; i++) {
						draw_line((prev2Ptr->x)-3, (prev2Ptr->y)-3+i, (prev2Ptr->x)+3, (prev2Ptr->y)-3+i, 0x0000);
					}
					prev2Ptr = prev2Ptr->next;
				}
			}
			
			// erase fruits in prev2 iteration
			i = 0;
			for (; i < NUM_FRUITS; i++){
				draw_image(x_fruitPrev2[i], y_fruitPrev2[i], erase,11,10);
			}			
			
			
			/* SAVE PREVIOUS LOCATION */
			
			// save previous location
			if (prev2.head != NULL) {
				deleteSnake(prev2.head);
				prev2.head = NULL;
			}
			copyList(&prev, &prev2);
			
			if (prev.head != NULL) {
				deleteSnake(prev.head);
				prev.head = NULL;
			}
			copyList(&cur, &prev);
			
			// save previous locations for fruits
			i = 0;
			for (; i < NUM_FRUITS; i++){
				x_fruitPrev2[i] = x_fruitPrev[i];
				y_fruitPrev2[i] = y_fruitPrev[i];
				x_fruitPrev[i] = x_fruitCur[i];
				y_fruitPrev[i] = y_fruitCur[i];
			}

			
			/* DRAW CURRENT LOCATIONS */
			
			// draw current positions of snake nodes
			Node *curPtr = cur.head;
			while (curPtr != NULL) {
				draw_image((curPtr->x)-2, (curPtr->y)-2, snakeBody, 5, 5);
				curPtr = curPtr->next;
			}
			
			// draw current positions of fruits
			i = 0;
			for (; i < NUM_FRUITS; i++){
				draw_image(x_fruitCur[i], y_fruitCur[i], apple, 11, 10);
			}

			
			/* UPDATE LOCATIONS */
			
			// update linked list values
			Node *curHead = cur.head;
			// KEY0 was pressed, move up
			if ((curDir == 0) && (prevDir != 1)) {
				insertAtFront(&cur, curHead->x, (curHead->y)-5);
				deleteBack(&cur);
			}
			// KEY1 was pressed, move down
			else if ((curDir == 1) && (prevDir != 0)) {
				insertAtFront(&cur, curHead->x, (curHead->y)+5);
				deleteBack(&cur);
			}
			// KEY2 was pressed, move left
			else if ((curDir == 2) && (prevDir != 3)) {
				insertAtFront(&cur, (curHead->x)-5, curHead->y);
				deleteBack(&cur);
			}
			// KEY3 was pressed, move right
			else if ((curDir == 3) && (prevDir != 2)) {
				insertAtFront(&cur, (curHead->x)+5, curHead->y);
				deleteBack(&cur);
			}
			prevDir = curDir;
			
			
			// check if snake eats the fruit
			i = 0;
			for (; i < NUM_FRUITS; i++){
				int fruit_x = 0;
				for (; fruit_x < 11; fruit_x++){
					int fruit_y = 0;
					for (; fruit_y < 10; fruit_y++){
						if (((curHead->x) == x_fruitCur[i] + fruit_x) && ((curHead->y) == y_fruitCur[i] + fruit_y)){
							score++;
							insertAtBack(&cur, curHead->x, curHead->y);
							
							// generate new apples
							x_fruitCur[i] = rand() % (RESOLUTION_X - 11); 
							y_fruitCur[i] = rand() % (RESOLUTION_Y - 10);
								
							// check if overlaps
							Node *curPtr = cur.head;
							while (curPtr != NULL) {
								if ((((curPtr->x)-2)==x_fruitCur[i]) && (((curPtr->y)-2)==y_fruitCur[i])){
									x_fruitCur[i] = rand() % (RESOLUTION_X - 11); 
									y_fruitCur[i] = rand() % (RESOLUTION_Y - 10);
								}		
								curPtr = curPtr->next;
							}
						}
					}		
				}
			}
			
			
			// update score on HEX display
			if (score <= 9){			
				*HEX3_0_ptr = seg7[score];
			} 
			else { 
				// display the score with lsl and orr operations
				int score_first_digit = score%10;
				int score_second_digit = score/10;
				*HEX3_0_ptr = (seg7[score_first_digit])| (seg7[(score_second_digit)]<<8);
			}
			
			
			// check if snake goes out of boundary
			if ((curHead->x <= 5) || (curHead->x >= (RESOLUTION_X-5))) {
				gameOver = true;
			}
			else if ((curHead->y <= 5) || (curHead->y >= (RESOLUTION_Y-5))) {
				gameOver = true;
			}
			
			// check if snake hits its own tail
			overlap = lines_overlap(&cur);
			
			wait_for_vsync(); // swap front and back buffers on VGA vertical sync
			pixel_buffer_start = *(pixel_ctrl_ptr + 1); // new back buffer
			
		}
		count++;
		
		if ((gameOver || overlap) && !gameOver_drawn) {
			
			finalScore(score);
			gameOver_drawn = true;

			draw_image(0, 0, gameOver_image, 320, 240);
			draw_score();
			wait_for_vsync();
    		pixel_buffer_start = *(pixel_ctrl_ptr + 1);
			draw_image(0, 0, gameOver_image, 320, 240);
			draw_score();

			deleteSnake(prev2.head);
			prev2.head = NULL;

			deleteSnake(prev.head);
			prev.head = NULL;

			deleteSnake(cur.head);
			cur.head = NULL;
		}
	}
}




/*
 * Initialize the banked stack pointer register for IRQ mode
*/

void set_A9_IRQ_stack(void) {
	int stack, mode;
	stack = A9_ONCHIP_END - 7; // top of A9 onchip memory, aligned to 8 bytes /* change processor to IRQ mode with interrupts disabled */
	mode = INT_DISABLE | IRQ_MODE;
	asm("msr cpsr, %[ps]" : : [ps] "r"(mode));
	/* set banked stack pointer */
	asm("mov sp, %[ps]" : : [ps] "r"(stack));
		/* go back to SVC mode before executing subroutine return! */
	mode = INT_DISABLE | SVC_MODE;
    asm("msr cpsr, %[ps]" : : [ps] "r"(mode));
}


/*
 * Turn on interrupts in the ARM processor
*/

void enable_A9_interrupts(void) {
	int status = SVC_MODE | INT_ENABLE;
    asm("msr cpsr, %[ps]" : : [ps] "r"(status));
}


/*
 * Configure the Generic Interrupt Controller (GIC)
*/

void config_GIC(void) {
	config_interrupt (79, 1); // configure the FPGA KEYs interrupt (73)
	// Set Interrupt Priority Mask Register (ICCPMR). Enable interrupts of all // priorities
	*((int *) 0xFFFEC104) = 0xFFFF;
	// Set CPU Interface Control Register (ICCICR). Enable signaling of // interrupts
	*((int *) 0xFFFEC100) = 1;
	// Configure the Distributor Control Register (ICDDCR) to send pending // interrupts to CPUs
	*((int *) 0xFFFED000) = 1;
}


/*
 * Configure Set Enable Registers (ICDISERn) and Interrupt Processor Target
* Registers (ICDIPTRn). The default (reset) values are used for other registers
* in the GIC.
*/

void config_interrupt(int N, int CPU_target) { 
	int reg_offset, index, value, address;
	reg_offset = (N >> 3) & 0xFFFFFFFC; 
	index = N & 0x1F;
	value = 0x1 << index;
	address = 0xFFFED100 + reg_offset;
	*(int *)address |= value;
	reg_offset = (N & 0xFFFFFFFC);
	index = N & 0x3;
	address = 0xFFFED800 + reg_offset + index;
	*(char *)address = (char)CPU_target;
}


// Define the IRQ exception handler

void __attribute__((interrupt)) __cs3_isr_irq(void) {
	// Read the ICCIAR from the processor interface
	int address = MPCORE_GIC_CPUIF + ICCIAR; 
	int int_ID = *((int *)address);
	if (int_ID == KEYS_IRQ) // check if interrupt is from the KEYs
		pushbutton_ISR();
	else if (int_ID == 79)
		PS2_ISR();
	else
		while (1)
			; // if unexpected, then stay here
	// Write to the End of Interrupt Register (ICCEOIR)
	address = MPCORE_GIC_CPUIF + ICCEOIR; 
	*((int *)address) = int_ID;
	return; 
}


// Define the remaining exception handlers

void __attribute__((interrupt))__cs3_reset(void) {
	while (1) 
		;
}

void __attribute__((interrupt))__cs3_isr_undef(void) {
	while (1) 
		;
}

void __attribute__((interrupt))__cs3_isr_swi(void) {
	while (1) 
		;
}

void __attribute__((interrupt))__cs3_isr_pabort(void) {
	while (1) 
		;
}

void __attribute__((interrupt)) __cs3_isr_dabort(void) {
	while (1) 
		;
}

void __attribute__((interrupt)) __cs3_isr_fiq(void) {
	while (1) 
		;
}


void config_KEYS(void) {
	volatile int * KEY_ptr = (int *) KEY_BASE; // pushbutton KEY address
    *(KEY_ptr + 2) = 0xF; // enable interrupts for all KEYs
}


void config_PS2(void) {
    volatile int * PS2_ptr = (int *) PS2_BASE; // PS/2 port address
    *(PS2_ptr) = 0xFF; /* reset */
    *(PS2_ptr + 1) = 0x1; /* write to the PS/2 Control register to enable interrupts */
}


void pushbutton_ISR(void) {
	volatile int * KEY_ptr = (int *) KEY_BASE;
	int press;
    press = *(KEY_ptr + 3); // read the pushbutton interrupt register
    *(KEY_ptr + 3) = press; // Clear the interrupt
    if ((press == 0b0001) && (curDir != 1))			// KEY[0]
        curDir = 0;
    else if ((press == 0b0010) && (curDir != 0))	// KEY[1]
        curDir = 1;
    else if ((press == 0b0100) && (curDir != 3))	// KEY[2]
        curDir = 2;
    else if ((press = 0b1000) && (curDir != 2))		// KEY[3]
        curDir = 3;
	else curDir = prevDir;
    return;
}


void PS2_ISR(void) {
	printf("KEY\n");
    volatile int * PS2_ptr = (int *)PS2_BASE;
	int PS2_data, RVALID;
	*(PS2_ptr) = 0xFF;
	
	while (1) {
		PS2_data = *(PS2_ptr);
    	RVALID = PS2_data & 0x8000;
    
    	if (RVALID) {
        	byte1 = byte2;
        	byte2 = byte3;
        	byte3 = PS2_data & 0xFF;
        
        	if ((byte1 == (char)0xE0) && (byte2 == (char)0xF0) && (byte3 == (char)0x75)) {
            	if (prevDir != 1) {
					curDir = 0;
					up();
				}
			}
        	else if ((byte1 == (char)0xE0) && (byte2 == (char)0xF0) && (byte3 == (char)0x72)) {
            	if (prevDir != 0) {
					curDir = 1;
					down();
				}
			}
        	else if ((byte1 == (char)0xE0) && (byte2 == (char)0xF0) && (byte3 == (char)0x6B)) {
            	if (prevDir != 3) {
					curDir = 2;
					left();
				}
			}
        	else if ((byte1 == (char)0xE0) && (byte2 == (char)0xF0) && (byte3 == (char)0x74)) {
            	if (prevDir != 2) {
					curDir = 3;
					right();
				}
			}
			else 
				curDir = prevDir;
    	}
		else {
			break;
		}
	}
	return;
}


void up() {
	Node *curHead = cur.head;
	insertAtFront(&cur, curHead->x, (curHead->y)-5);
	deleteBack(&cur);
	prevDir = curDir;
}


void down() {
	Node *curHead = cur.head;
	// KEY1 was pressed, move down
	insertAtFront(&cur, curHead->x, (curHead->y)+5);
	deleteBack(&cur);
	
	prevDir = curDir;
}


void left() {
	Node *curHead = cur.head;
	// KEY2 was pressed, move left
	insertAtFront(&cur, (curHead->x)-5, curHead->y);
	deleteBack(&cur);
	prevDir = curDir;
}
	
	
void right() {
	Node *curHead = cur.head;
	// KEY3 was pressed, move right
	insertAtFront(&cur, (curHead->x)+5, curHead->y);
	deleteBack(&cur);
	prevDir = curDir;
}


// assume player's score does not go beyond 99
void finalScore(int curScore) {
	int ones, tenth;
	ones = curScore % 10;
	curScore = curScore/10;
	tenth = curScore % 10;
	score_ones = ones;
	score_tenth = tenth;
	return;
}


// draw the player's final score onto the game over screen
void draw_score() {
	
	// draw the tenth digit
	if (score_tenth == 1) {
		draw_image(180, 115, one, 25, 25);
	}
	else if (score_tenth == 2) {
		draw_image(180, 115, two, 25, 25);
	}
	else if (score_tenth == 3) {
		draw_image(180, 115, three, 25, 25);
	}
	else if (score_tenth == 4) {
		draw_image(180, 115, four, 25, 25);
	}
	else if (score_tenth == 5) {
		draw_image(180, 115, five, 25, 25);
	}
	else if (score_tenth == 6) {
		draw_image(180, 115, six, 25, 25);
	}
	else if (score_tenth == 7) {
		draw_image(180, 115, seven, 25, 25);
	}
	else if (score_tenth == 8) {
		draw_image(180, 115, eight, 25, 25);
	}
	else if (score_tenth == 9) {
		draw_image(180, 115, nine, 25, 25);
	}
	
	// draw the ones digit
	if (score_ones == 0) {	
		draw_image(195, 115, zero, 25, 25);
	}
	else if (score_ones == 1) {	
		draw_image(195, 115, one, 25, 25);
	}
	else if (score_ones == 2) {	
		draw_image(195, 115, two, 25, 25);
	}
	else if (score_ones == 3) {	
		draw_image(195, 115, three, 25, 25);
	}
	else if (score_ones == 4) {	
		draw_image(195, 115, four, 25, 25);
	}
	else if (score_ones == 5) {	
		draw_image(195, 115, five, 25, 25);
	}
	else if (score_ones == 6) {	
		draw_image(195, 115, six, 25, 25);
	}
	else if (score_ones == 7) {	
		draw_image(195, 115, seven, 25, 25);
	}
	else if (score_ones == 8) {	
		draw_image(195, 115, eight, 25, 25);
	}
	else if (score_ones == 9) {	
		draw_image(195, 115, nine, 25, 25);
	}
}
	
	
void reset() {
	*(LEDR_ptr) = 0;
	newGame = true;
    gameOver = false;
	mode_selected = false;
	run = false;
	instructions_cleared = false;
    score = 0;
	gameOver_drawn = false;
	overlap = false;
	count = 0;
	
	int x_val = (0 + RESOLUTION_X)/2;
	int y_val = (0 + RESOLUTION_Y)/2;

	int i = 0;
	for (; i < 100; i+=5) {
		insertAtBack(&cur, x_val-i, y_val);
	}
	return;
}



/* 	check if two lines intersect (if snake hits its own tail)
	line1 is the head seg, line2 is the other seg in comparison
	lines NEVER intersect when in parallel
	slopes are either 0 or undefined
	
	when head seg is vertical, lines intersect if:
	1) x-coord of head seg is b/w x-coord of other seg, AND
	2) y-coord of other seg is b/w y-coord of head seg
	
	when head seg is horizontal, lines intersect if:
	1) y-coord of head seg is b/w y-coord of other seg, AND
	2) x-coord of other seg is b/w x-coord of head seg
*/

bool two_lines_intersect(int x1_start, int y1_start, int x1_end, int y1_end, 
						 int x2_start, int y2_start, int x2_end, int y2_end) {
	// if lines are parallel
	// both lines are vertical
	if ((x1_start == x1_end) && (x2_start == x2_end)) {
		return false;
	}
	// both lines are horizontal
	if ((y1_start == y1_end) && (y2_start == y2_end)) {
		return false;
	}
	// if lines are NOT in parallel
	// if head seg is vertical
	if (x1_start == x1_end) {
		if ((x1_start >= x2_start) && (x1_start <= x2_end)) {
			if ((y2_start >= y1_start) && (y2_start <= y1_end)) {
				return true;
			}
		}
	}
	// if head seg is horizontal
	if (y1_start == y1_end) {
		if ((y1_start >= y2_start) && (y1_start <= y2_end)) {
			if ((x2_start >= x1_start) && (x2_start <= x1_end)) {
				return true;
			}
		}
	}
	return false;
}



// check the snake hits its own tail
bool lines_overlap(Snake *list) {
	Node *curHead = list->head;
	Node *curPtr = list->head->next;
	int xHeadStart, yHeadStart, xHeadEnd, yHeadEnd;
	
	// compare the outer segment of head against all side segments of snake
	while (curPtr != NULL) {
		// if travelling up, check top segment of head
		if (curDir == 0) {
			xHeadStart = (curHead->x)-2;
			yHeadStart = (curHead->y)-2;
			xHeadEnd = (curHead->x)+2;
			yHeadEnd = (curHead->y)-2;
		}
		// if travelling down, check bottom segment of head
		else if (curDir == 1) {
			xHeadStart = (curHead->x)-2;
			yHeadStart = (curHead->y)+2;
			xHeadEnd = (curHead->x)+2;
			yHeadEnd = (curHead->y)+2;
		}
		// if travellig left, check left segment
		else if (curDir == 2) {
			xHeadStart = (curHead->x)-2;
			yHeadStart = (curHead->y)-2;
			xHeadEnd = (curHead->x)-2;
			yHeadEnd = (curHead->y)+2;
		}
		// if travelling right, check right segment
		else if (curDir == 3) {
			xHeadStart = (curHead->x)+2;
			yHeadStart = (curHead->y)-2;
			xHeadEnd = (curHead->x)+2;
			yHeadEnd = (curHead->y)+2;
		}
		
		// left segment of current node
		if (two_lines_intersect(xHeadStart, yHeadStart, xHeadEnd, yHeadEnd,
								(curPtr->x)-2, (curPtr->y)-2, (curPtr->x)-2, (curPtr->y)+2)) {
			return true;
		}
		// top segment of current node
		else if (two_lines_intersect(xHeadStart, yHeadStart, xHeadEnd, yHeadEnd,
								(curPtr->x)-2, (curPtr->y)-2, (curPtr->x)+2, (curPtr->y)-2)) {
			return true;
		}
		// right segment of current node
		else if (two_lines_intersect(xHeadStart, yHeadStart, xHeadEnd, yHeadEnd,
								(curPtr->x)+2, (curPtr->y)-2, (curPtr->x)+2, (curPtr->y)+2)) {
			return true;
		}
		// bottom segment of current node
		else if (two_lines_intersect(xHeadStart, yHeadStart, xHeadEnd, yHeadEnd,
								(curPtr->x)-2, (curPtr->y)+2, (curPtr->x)+2, (curPtr->y)+2)) {
			return true;
		}
		curPtr = curPtr->next;
	}
	return false;
}


void clear_screen() {
	int i = 0;
	for (; i < RESOLUTION_X; i++) {
		int j = 0;
		for (; j < RESOLUTION_Y; j++) {
			plot_pixel(i, j, 0x0000);
		}
	}
}


void draw_line(int x0, int y0, int x1, int y1, short int colour) {
	bool is_steep = abs(y1 - y0) > abs(x1 - x0);
	if (is_steep) {
		swap(&x0, &y0);
		swap(&x1, &y1);
	}
	if (x0 > x1) {
		swap(&x0, &x1);
		swap(&y0, &y1);
	}
	int deltax = x1 - x0;
	int deltay = abs(y1 - y0);
	int error = -(deltax / 2);
	int y = y0;
	int y_step = 0;
	if (y0 < y1)
		y_step = 1;
	else 
		y_step = -1;
	int x = x0;
	for (; x < x1; x++) {
		if (is_steep)
			plot_pixel(y, x, colour);
		else
			plot_pixel(x, y, colour);
 		error = error + deltay;
		if (error >= 0) {
			y = y + y_step;
			error = error - deltax;
		}
	}
}


void swap(int* x, int* y) {
	int temp = *x;
	*x = *y; 
	*y = temp;
}


void plot_pixel(int x, int y, short int line_color) {
    *(short int *)(pixel_buffer_start + (y << 10) + (x << 1)) = line_color;
}


void wait_for_vsync() {
	volatile int *pixel_ptr = (int*)PIXEL_BUF_CTRL_BASE;
	int status;
	*pixel_ptr = 1;
	status = *(pixel_ptr + 3);
	while ((status & 0x01) != 0) {
		status = *(pixel_ptr + 3);
	}
}


void draw_image(int initial_x, int initial_y, int imageArray[], unsigned width, unsigned height){
    int i = 0;
    unsigned y, x;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int plot_x = initial_x + x;
            int plot_y = initial_y + y;
            if (plot_x >= 0 && plot_y >= 0 && plot_x < 320 && plot_y < 240){
                plot_pixel(plot_x, plot_y, imageArray[i]);
            }
            i++;
        }
    }
}


void draw_borders() {
	int i = 1;
	for (; i <= 3; i++) {
		draw_line(1, i, (RESOLUTION_X-1), i, PINK);
		draw_line(i, 1, i, (RESOLUTION_Y-1), PINK);
		draw_line((RESOLUTION_X-i), 1, (RESOLUTION_X-i), (RESOLUTION_Y-1), PINK);
		draw_line(1, (RESOLUTION_Y-i), (RESOLUTION_X-1), (RESOLUTION_Y-i), PINK);
	}
}


Node *createNode(int x_val, int y_val) {
    Node *newNode = (Node *) malloc(sizeof(Node));
    if (newNode != NULL) {
        newNode->x = x_val;
        newNode->y = y_val;
        newNode->next = NULL;
    }
    return newNode;
}


bool insertAtFront(Snake *list, int x_val, int y_val) {
    Node *temp = createNode(x_val, y_val);
    if (temp == NULL) {
        return false;
    }
    temp->next = list->head;
    list->head = temp;
    return true;
}


bool insertAtBack(Snake *list, int x_val, int y_val) {
    if (list->head == NULL) {
        return insertAtFront(list, x_val, y_val);
    }
    Node *current = list->head;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = createNode(x_val, y_val);
    if (current->next == NULL) {
        return false;
    }
    return true;
}


void deleteBack(Snake *list) {
    Node *current = list->head;
    while (current->next->next != NULL) {
        current = current->next;
    }
    free(current->next);
    current->next = NULL;
}


void deleteSnake(Node *head) {
    if (head == NULL) 
        return;
    deleteSnake(head->next); 
    free(head);   
}


bool copyList(Snake *listOrg, Snake *listCopy) {
    Node *temp = listOrg->head;
    if (temp == NULL)
        return false;
    while (temp != NULL) {
        insertAtBack(listCopy, temp->x, temp->y);
        temp = temp->next;
    }
    return true;
}