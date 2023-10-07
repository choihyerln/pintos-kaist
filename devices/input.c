#include "devices/input.h"
#include <debug.h>
#include "devices/intq.h"
#include "devices/serial.h"

/* Stores keys from the keyboard and serial port. */
static struct intq buffer;

/* Initializes the input buffer. */
void
input_init (void) {
	intq_init (&buffer);
}

/* Adds a key to the input buffer.
   Interrupts must be off and the buffer must not be full. */
void
input_putc (uint8_t key) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (!intq_full (&buffer));

	intq_putc (&buffer, key);
	serial_notify ();
}

/* 입력 버퍼에서 키를 검색합니다.
만약 버퍼가 비어있다면, 키가 눌릴 때까지 기다립니다. */
uint8_t
input_getc (void) {
	enum intr_level old_level;
	uint8_t key;

	old_level = intr_disable ();
	key = intq_getc (&buffer);
	serial_notify ();
	intr_set_level (old_level);

	return key;
}

/* 인풋 버퍼가 가득 찼을 때 true를 반환하며,
   그렇지 않으면 false를 반환합니다.
   인터럽트는 비활성화되어야 합니다. */
bool
input_full (void) {
	ASSERT (intr_get_level () == INTR_OFF);
	return intq_full (&buffer);
}
