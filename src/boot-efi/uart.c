#include "uart.h"

static UINT8 inb(UINT16 port) {
    UINT8 v;
    __asm__ volatile("inb %w1, %b0" : "=a"(v) : "Nd"(port));
    return v;
}

static void outb(UINT16 port, UINT8 v) {
    __asm__ volatile("outb %b0, %w1" : : "a"(v), "Nd"(port));
}

int uart_probe(UINT16 base) {
    UINT8 orig = inb(base + 7);
    UINT8 v1, v2;
    outb(base + 7, 0xAA); v1 = inb(base + 7);
    outb(base + 7, 0x55); v2 = inb(base + 7);
    outb(base + 7, orig);
    return (v1 == 0xAA) && (v2 == 0x55);
}
