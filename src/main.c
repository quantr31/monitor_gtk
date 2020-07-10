/**************************************************
 * Monitoring screen application with initial function: 
 * - Setup the timer, then count down from the timer 
 * - Creat another thread to receive input from dry contact
 * using external circuit, change the display when the contact closes
 * - Using button on GTK/glade design to control relay module 
 * Author: Quan T.V.V 
 * Company: LFS 
 * Date: July 1st 2020
 * ************************************************/

#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <bcm2835.h>
#include <glib.h>
//include header for modbus prog
#include <unistd.h>
#include <modbus.h>
#include <stdio.h>
#include <errno.h>
//inlude header for spi
#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include "gz_clk.h"

//declaration for SPI ADC
static const char *ADC_SPI = "/dev/spidev0.0";
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static uint8_t mode = SPI_CPHA | SPI_CPOL;
static uint8_t bits = 8;
static uint32_t speed = 5;
static uint16_t delay = 10;

//declaration for MODBUS RTU unit
#define SERVER_ID 1
const uint8_t req[] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x02, 0x71, 0xCB};
uint8_t req_length;

//LED output pin 
#define PIN_OUT RPI_GPIO_P1_11
#define PIN_IN RPI_GPIO_P1_15

//mutex lock to protect access to memory when threading
GMutex mutex_lock_1;
GMutex mutex_lock_2;
uint8_t is_contact;

//widgets struct
typedef struct {
    GtkWidget *timer_lbl;
    GtkWidget *clock_lbl;
    GtkWidget *light_lbl;
    GtkWidget *contact_lbl;
    GtkWidget *input_hrs;
    GtkWidget *input_mnt;
    GtkWidget *input_sec;
    //clock var
    int16_t hrs;
    int16_t mnt;
    int16_t sec;
    int16_t data;
    //sensor var
    uint8_t *rsp;
    
} app_widgets;

static void pabort(const char *s)
{
	perror(s);
	abort();
}

static void writeReset(int fd)
{
	int ret;
	uint8_t tx1[5] = {0xff,0xff,0xff,0xff,0xff};
	uint8_t rx1[5] = {0};
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx1,
		.rx_buf = (unsigned long)rx1,
		.len = ARRAY_SIZE(tx1),
		.delay_usecs = delay,
		.speed_hz = speed,
		.bits_per_word = bits,
	};

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
		pabort("can't send spi message");
}

static void writeReg(int fd, uint8_t v)
{
	int ret;
	uint8_t tx1[1];
	tx1[0] = v;
	uint8_t rx1[1] = {0};
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx1,
		.rx_buf = (unsigned long)rx1,
		.len = ARRAY_SIZE(tx1),
		.delay_usecs = delay,
		.speed_hz = speed,
		.bits_per_word = bits,
	};

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
		pabort("can't send spi message");

}

static uint8_t readReg(int fd)
{
	int ret;
	uint8_t tx1[1];
	tx1[0] = 0;
	uint8_t rx1[1] = {0};
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx1,
		.rx_buf = (unsigned long)rx1,
		.len = ARRAY_SIZE(tx1),
		.delay_usecs = delay,
		.speed_hz = speed,
		.bits_per_word = 8,
	};

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
	  pabort("can't send spi message");
	  
	return rx1[0];
}

static int readData(int fd)
{
	int ret;
	uint8_t tx1[2] = {0,0};
	uint8_t rx1[2] = {0,0};
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx1,
		.rx_buf = (unsigned long)rx1,
		.len = ARRAY_SIZE(tx1),
		.delay_usecs = delay,
		.speed_hz = speed,
		.bits_per_word = 8,
	};

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
	  pabort("can't send spi message");
	  
	return (rx1[0]<<8)|(rx1[1]);
}

//real clock testing
gboolean clock_timer(app_widgets *widgets)
{
    GDateTime *date_time;
    gchar *dt_format;
    
    date_time = g_date_time_new_now_local();
    dt_format = g_date_time_format(date_time, "%H:%M:%S");
    gtk_label_set_text(GTK_LABEL(widgets->clock_lbl), dt_format);
    g_free(dt_format);
    
    return TRUE;
    }

//flow: read entry input, push button to create countdown timer, then come back to main function
//to count down for each second

gboolean timer_handler(app_widgets *widgets)
{
    widgets->data--;
    gchar *time = g_strdup_printf("%d", widgets->data);
    gtk_label_set_text(GTK_LABEL(widgets->timer_lbl), time);
    g_free(time);
    if(widgets->data > 0)
    {return 1;}
    else return 0;
}

//callback function to countdown the clock
gboolean countdown(app_widgets *widgets)
{
    widgets->sec--;
    if(widgets->sec < 0) 
        {
            widgets->sec = 59;
            widgets->mnt--;
            if(widgets->mnt < 0)
            {
                widgets->mnt = 59;
                widgets->hrs--;
                }
            }
    gchar *count = g_strdup_printf("%d : %d : %d", widgets->hrs, widgets->mnt, widgets->sec);
    gtk_label_set_text(GTK_LABEL(widgets->timer_lbl), count);
    g_free(count);
    if((widgets->hrs == 0)&&(widgets->mnt == 0) && (widgets->sec == 0)) {return 0;}
    else return 1;
    }

//callback function for read button to initialize the clock timer
void on_btn_read_clicked(GtkButton *button, app_widgets *widgets)
{
    widgets->hrs = atoi(gtk_entry_get_text(GTK_ENTRY(widgets->input_hrs)));
    widgets->mnt = atoi(gtk_entry_get_text(GTK_ENTRY(widgets->input_mnt)));
    widgets->sec = atoi(gtk_entry_get_text(GTK_ENTRY(widgets->input_sec)));
    gchar *test = g_strdup_printf("%d : %d : %d", widgets->hrs, widgets->mnt, widgets->sec);
    gtk_label_set_text(GTK_LABEL(widgets->timer_lbl), test);
    g_free(test);
}

void on_btn_count_clicked(GtkButton *button, app_widgets *widgets)
{
    g_timeout_add_seconds(1, (GSourceFunc)countdown, widgets);
}

void relay1_control(app_widgets *widgets)
{
      bcm2835_gpio_fsel(PIN_OUT, BCM2835_GPIO_FSEL_OUTP);
      while(1)
      {
            bcm2835_gpio_write(PIN_OUT, HIGH);
            delay(500);
            bcm2835_gpio_write(PIN_OUT, LOW);
            delay(500);
            while(gtk_events_pending()){gtk_main_iteration();}
      } 
      bcm2835_close(); 
}

void on_btn_light_clicked(GtkButton *button, app_widgets *widgets)
{
        g_thread_new(NULL, (GThreadFunc)relay1_control, (app_widgets*) widgets);
}
/*********
gboolean update_func(app_widgets *widgets)
{
    g_mutex_lock(&mutex_lock_1);
    widgets->data = 1;
    gchar *text1 = g_strdup_printf("%d", widgets->data);
    gtk_label_set_text(GTK_LABEL(widgets->light_lbl), text1);
    g_free(text1);
    g_mutex_unlock(&mutex_lock_1);
    return TRUE;
}
********/
/************************************
 * handler when pin 15 is pulled up
 * **********************************/
gboolean display_dry_contact(app_widgets *widgets)
{
    g_mutex_lock(&mutex_lock_2);
    widgets->data = 2;
    gchar *text2 = g_strdup_printf("%d", widgets->data);
    gtk_label_set_text(GTK_LABEL(widgets->contact_lbl),text2);
    g_free(text2);
    g_mutex_unlock(&mutex_lock_2);
    return TRUE;
}
/**************************************
 * handler when pin 15 is pulled down
 * ***********************************/
gboolean display_dry_contact_1(app_widgets *widgets)
{
    g_mutex_lock(&mutex_lock_2);
    widgets->data = 3;
    gchar *text2 = g_strdup_printf("%d", widgets->data);
    gtk_label_set_text(GTK_LABEL(widgets->contact_lbl),text2);
    g_free(text2);
    g_mutex_unlock(&mutex_lock_2);
    return TRUE;
}

//this function is one of the main thread to check for dry contact, normally pin 15 is pulled up
//, if it's pulled to GND then other function will be called
void check_dry_contact(app_widgets *widgets)
{
    // Set RPI pin P1-15 to be an input
    bcm2835_gpio_fsel(PIN_IN, BCM2835_GPIO_FSEL_INPT);
    //  with a pullup
    bcm2835_gpio_set_pud(PIN_IN, BCM2835_GPIO_PUD_UP);
    while(1)
    {
            is_contact = bcm2835_gpio_lev(PIN_IN);
            if(is_contact == 1)
            {
            gdk_threads_add_idle((GSourceFunc)display_dry_contact, widgets);
            }
            else if (is_contact == 0)
            {
            gdk_threads_add_idle((GSourceFunc)display_dry_contact_1, widgets);
            }
            delay(500);
            while(gtk_events_pending()){gtk_main_iteration();}
    }
    bcm2835_close();
}

//this function is one of the main thread to send a query to modbus sensor compliant with datasheet, then the 
//sensor will return an array of data to read and update in app_widgets structure
//and ready for further processing
int read_modbus_sensor(app_widgets *widgets)
{
    modbus_t *ctx;
    //allocate memory for sensor reading
    widgets->rsp = (uint8_t*) malloc(MODBUS_RTU_MAX_ADU_LENGTH * sizeof(uint8_t));
    memset(widgets->rsp, 0, MODBUS_RTU_MAX_ADU_LENGTH * sizeof(uint8_t));
    //create new connect to RTU
    ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 1);
    modbus_set_slave(ctx, SERVER_ID);
    
    if (modbus_connect(ctx) == -1) {
        printf("Connection failed: %s\n",
        modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
		}
		else 
        {printf("Connection succeeded\n");}
        
    req_length = modbus_send_raw_request(ctx, req, 8*sizeof(uint8_t));
    if(req_length < 0) {printf("read failed :(\n");}
	modbus_receive_confirmation(ctx, widgets->rsp);
	for(int i = 0; i<10;i++)
		{
			printf("%x\n", widgets->rsp[i]);
        }
			
		free(widgets->rsp);
		modbus_close(ctx);
		modbus_free(ctx);
        return 0;
}

void readADC(app_widgets *widgets)
{
        int ret = 0;
        int fd = open(ADC_SPI, O_RDWR);
        if(fd < 0) printf("Can't open device");
        ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
        if(ret  == -1) printf("Can't set SPI mode");
        ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
        if(ret == -1) printf("Can't get SPI mode");
        ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
        if(ret == -1) printf("Can't set bits per word");
        ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
        if(ret == -1) printf("Can't get bits per word");
        ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
        if(ret == -1) printf("Can't set max speed Hz");
        ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
        if(ret == -1) printf("Can't get max speed Hz");
        fprintf(stderr, "spi mode: %d \n", mode);
        fprintf(stderr, "bits per word: %d\n", bits);
        fprintf(stderr, "max speed: %d Hz\n", speed);
        //enable master clock
        gz_clock_ena(GZ_CLK_5MHz, 5);
        //reset the AD7705
        writeReset(fd);
        //tell AD7705 that the next write will be to the clock reg
        writeReg(fd, 0x20);
        //write 00001100: CLOCKDIV=1, CLK=1, expects 4.9152MHz input clock
        writeReg(fd, 0x0C);
        //tell AD7705 that the next write will be the setup reg
        writeReg(fd, 0x10);
        //initiate a self calib then start converting
        writeReg(fd, 0x40);
        //read data
        while(1){
            int d=0;
            do{
                writeReg(fd, 0x08);
                d = readReg(fd);
                } while (d & 0x80);
                
            //read the data reg
            writeReg(fd, 0x38);
            int value = readData(fd) - 0x8000;
            printf("data = %d \n", value);
            }
        close(fd);
}

/*********************
int LED_control(app_widgets *widgets)
{
    bcm2835_gpio_fsel(PIN_OUT, BCM2835_GPIO_FSEL_OUTP);
    while(1)
    {
    bcm2835_gpio_write(PIN_OUT, HIGH);
    delay(500);
    bcm2835_gpio_write(PIN_OUT, LOW);
    delay(500);
    
    gdk_threads_add_idle((GSourceFunc)update_func, widgets);
    while(gtk_events_pending()){gtk_main_iteration();}
    }
    bcm2835_close();
    return 1;
    }
********************/
    
int main(int argc, char *argv[])
{    
    //init glade 
    GtkBuilder      *builder; 
    GtkWidget       *window;
    app_widgets *widgets = g_slice_new(app_widgets);
    
    //init bcm2835 lib
    if(!bcm2835_init())
    return 1;
    //thread to read ADC via SPI
    g_thread_new(NULL, (GThreadFunc)readADC, (app_widgets*) widgets);
    //thread to check dry contact
    g_thread_new(NULL, (GThreadFunc)check_dry_contact, (app_widgets*) widgets);
    //thread to read modbus sensor 
    g_thread_new(NULL, (GThreadFunc)read_modbus_sensor, (app_widgets*) widgets);
    
    gtk_init(&argc, &argv);
    builder = gtk_builder_new_from_file("glade/window_main.glade");
    window = GTK_WIDGET(gtk_builder_get_object(builder, "window_main"));
    gtk_widget_set_size_request(GTK_WIDGET(window), 1920, 1080);
    
    widgets->input_hrs = GTK_WIDGET(gtk_builder_get_object(builder, "entry_input_hrs"));
    widgets->input_mnt = GTK_WIDGET(gtk_builder_get_object(builder, "entry_input_mnt"));
    widgets->input_sec = GTK_WIDGET(gtk_builder_get_object(builder, "entry_input_sec"));
    widgets->timer_lbl = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_text"));
    widgets->clock_lbl = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_clock"));
    widgets->light_lbl = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_light"));
    widgets->contact_lbl = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_contact"));

    gtk_builder_connect_signals(builder, widgets);
    g_object_unref(builder);
    gtk_widget_show(window);

    gtk_main();
    g_slice_free(app_widgets, widgets);
    return 0;
}

// called when window is closed
void on_window_main_destroy()
{
    gtk_main_quit();
}







