#pragma once
#include <stdint.h>

// Базовые классы USB
#define USB_CLASS_HID 0x03

// Инициализация всего USB-стека
void usb_init(void);