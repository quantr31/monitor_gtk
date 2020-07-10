#ifndef __SPI_H_
#define __SPI_H_

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static uint8_t mode = SPI_CPHA | SPI_CPOL;
static uint8_t bits = 8;
static uint32_t speed = 5;
static uint16_t delay = 10;

static void writeReset(int fd);
static void writeReg(int fd, uint8_t v);
static uint8_t readReg(int fd);
static int readData(int fd);

#endif
