/*
	simduino.c

	Copyright 2008, 2009 Michel Pollet <buserror@gmail.com>

 	This file is part of simavr.

	simavr is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	simavr is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with simavr.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>

#include <GL/glut.h>
#include <pthread.h>

#include "sim_avr.h"
#include "avr_ioport.h"
#include "sim_elf.h"
#include "sim_hex.h"
#include "sim_gdb.h"
#include "uart_udp.h"
#include "sim_vcd_file.h"

#include "button.h"

button_t button;
uart_udp_t uart_udp;
int do_button_press = 0;
avr_t * avr = NULL;
avr_vcd_t vcd_file;
uint8_t	pin_state = 0;	// current port B

float pixsize = 64;
int window;

/*
 * called when the AVR change any of the pins on port B
 * so lets update our buffer
 */
void pin_changed_hook(struct avr_irq_t * irq, uint32_t value, void * param)
{
//	pin_state = (pin_state & ~(1 << irq->irq)) | (value << irq->irq);
}

void displayCB(void)		/* function called whenever redisplay needed */
{
	// OpenGL rendering goes here...
	glClear(GL_COLOR_BUFFER_BIT);

	// Set up modelview matrix
	glMatrixMode(GL_MODELVIEW); // Select modelview matrix
	glLoadIdentity(); // Start with an identity matrix

	float grid = pixsize;
	float size = grid * 0.8;
    glBegin(GL_QUADS);
	glColor3f(1,0,0);

#if 0
	for (int di = 0; di < 8; di++) {
		char on = (pin_state & (1 << di)) != 0;
		if (on) {
			float x = (di) * grid;
			float y = 0; //(si * grid * 8) + (di * grid);
			glVertex2f(x + size, y + size);
			glVertex2f(x, y + size);
			glVertex2f(x, y);
			glVertex2f(x + size, y);
		}
	}
#endif
    glEnd();
    glutSwapBuffers();
    //glFlush();				/* Complete any pending operations */
}

void keyCB(unsigned char key, int x, int y)	/* called on key press */
{
	if (key == 'q')
		exit(0);
	static uint8_t buf[64];
	switch (key) {
		case 'q':
		case 0x1f: // escape
			exit(0);
			break;
		case ' ':
			do_button_press++; // pass the message to the AVR thread
			break;
		case 'r':
			printf("Starting VCD trace\n");
			avr_vcd_start(&vcd_file);
			break;
		case 's':
			printf("Stopping VCD trace\n");
			avr_vcd_stop(&vcd_file);
			break;
	}
}

// gl timer. if the pin have changed states, refresh display
void timerCB(int i)
{
	static uint8_t oldstate = 0xff;
	// restart timer
	glutTimerFunc(1000/64, timerCB, 0);
#if 0
	if (oldstate != pin_state) {
		oldstate = pin_state;
		glutPostRedisplay();
	}
#endif
}

static void * avr_run_thread(void * oaram)
{
	int b_press = do_button_press;

	while (1) {
		avr_run(avr);
#if 0
		if (do_button_press != b_press) {
			b_press = do_button_press;
			printf("Button pressed\n");
			button_press(&button, 1000000);
		}
#endif
	}
}


int main(int argc, char *argv[])
{
	elf_firmware_t f;
	const char * pwd = dirname(argv[0]);

	avr = avr_make_mcu_by_name("atmega328p");
	if (!avr) {
		fprintf(stderr, "%s: Error creating the AVR core\n", argv[0]);
		exit(1);
	}
	avr_init(avr);
	avr->frequency = 16000000;

	// this trick creates a file that contains /and keep/ the flash
	// in the same state as it was before. This allow the bootloader
	// app to be kept, and re-run if the bootloader doesn't get a
	// new one
	{
		char path[256];
		sprintf(path, "%s/%s", pwd, "simduino_flash.bin");

		int fd = open(path, O_RDWR|O_CREAT, 0644);
		if (fd < 0) {
			perror(path);
			exit(1);
		}
		ftruncate(fd, avr->flashend + 1);
		uint8_t * mm = (uint8_t*)mmap(NULL, avr->flashend + 1 /* 32k is multiple of 4096 */,
				PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		if (!mm) {
			perror(path);
			exit(1);
		}

		// reload bootloader anyway
		free(avr->flash);
		avr->flash = mm;

		uint32_t base, size;
		sprintf(path, "%s/%s", pwd, "ATmegaBOOT_168_atmega328.ihex");
		uint8_t * boot = read_ihex_file(path, &size, &base);
		if (!boot) {
			fprintf(stderr, "%s: Unable to load %s\n", argv[0], path);
			exit(1);
		}
		printf("Booloader %04x: %d\n", base, size);
		memcpy(mm + base, boot, size);
		free(boot);
		avr->pc = base;
		avr->codeend = avr->flashend;
	}
	//avr->trace = 1;

	// even if not setup at startup, activate gdb if crashing
	avr->gdb_port = 1234;
	if (0) {
		//avr->state = cpu_Stopped;
		avr_gdb_init(avr);
	}

	uart_udp_init(avr, &uart_udp);
	uart_udp_connect(&uart_udp, '0');

	/*
	 * OpenGL init, can be ignored
	 */
	glutInit(&argc, argv);		/* initialize GLUT system */

	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
	glutInitWindowSize(8 * pixsize, 1 * pixsize);		/* width=400pixels height=500pixels */
	window = glutCreateWindow("Glut");	/* create window */

	// Set up projection matrix
	glMatrixMode(GL_PROJECTION); // Select projection matrix
	glLoadIdentity(); // Start with an identity matrix
	glOrtho(0, 8 * pixsize, 0, 1 * pixsize, 0, 10);
	glScalef(1,-1,1);
	glTranslatef(0, -1 * pixsize, 0);

	glutDisplayFunc(displayCB);		/* set window's display callback */
	glutKeyboardFunc(keyCB);		/* set window's key callback */
	glutTimerFunc(1000 / 24, timerCB, 0);

	// the AVR run on it's own thread. it even allows for debugging!
	pthread_t run;
	pthread_create(&run, NULL, avr_run_thread, NULL);

	glutMainLoop();
}
