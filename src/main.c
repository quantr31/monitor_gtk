/**************************************************
 * Monitoring screen application with initial function: 
 * - Setup the timer, then count down from the timer 
 * - Creat another thread to receive input from dry contact
 * using external circuit, change the display when the contact closes
 * - Using button on GTK/glade design to control relay module 
 * - Read RTU modbus sensor to display temperature and humidity 
 * - Read ADC value from differential pressure sensor
 * Author: Quan T.V.V 
 * Company: LFS 
 * Date: July 1st 2020
 * ************************************************/
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
//system access
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
//external lib
#include <gtk/gtk.h>
#include <bcm2835.h>
#include <glib.h>
#include <time.h>
#include <modbus.h>
#include "gz_clk.h"

//declaration for SPI ADC
static const char *ADC_SPI = "/dev/spidev0.0";
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static uint8_t mode = SPI_CPHA | SPI_CPOL;
static uint8_t bits = 8;
static uint8_t speed = 5;
static uint8_t delay = 10;

static void writeReset(int fd);
static void writeReg(int fd, uint8_t v);
static uint8_t readReg(int fd);
static int readData(int fd);

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
GMutex mutex_lock_3;
GMutex mutex_lock_4;
uint8_t is_contact;

//widgets struct
typedef struct {
    GtkStack *stack;
    //page 0
    //spin button
    GtkWidget *hrs_op_in;
    GtkWidget *mnt_op_in;
    GtkWidget *sec_op_in;
    GtkWidget *hrs_an_in;
    GtkWidget *mnt_an_in;
    GtkWidget *sec_an_in;
    //button 
    GtkWidget *btn_set;
    GtkWidget *btn_run;
    GtkWidget *btn_reset;
    GtkWidget *btn_shut;
    
    GtkWidget *btn1;
    GtkWidget *btn2;
    GtkWidget *btn3;
    GtkWidget *btn4;
    
    GtkWidget *spin_temp;
    GtkWidget *spin_hu;
    
    GtkWidget *lbl_real_temp;
    GtkWidget *lbl_real_hu;
    //page 1
    GtkWidget *btn_op_start;
    GtkWidget *btn_an_start;
    GtkWidget *lbl_op_hrs;
    GtkWidget *lbl_op_mnt;
    GtkWidget *lbl_op_sec;
    GtkWidget *lbl_an_hrs;
    GtkWidget *lbl_an_mnt;
    GtkWidget *lbl_an_sec;
    
    GtkWidget *lbl_date;
    GtkWidget *lbl_time;
    GtkWidget *lbl_temp;
    GtkWidget *lbl_hu;
    
    GtkWidget *btn_run_back;
    GtkWidget *btn_run_reset;
    GtkWidget *btn_run_shut;
    
    //clock var
    int8_t op_hrs;
    int8_t op_mnt;
    int8_t op_sec;
    int8_t an_hrs;
    int8_t an_mnt;
    int8_t an_sec;
    int8_t data;
    //sensor var
    uint8_t *rsp;
    uint16_t temp;
    uint16_t humid;
    //adc var
    uint16_t adc_val;
    //adjust temp&humidity
    uint8_t adj_temp;
    uint8_t adj_hu;
} app_widgets;

static void pabort(const char *s)
{
	perror(s);
	abort();
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
	widgets->temp = ((widgets->rsp[3]<<8)|widgets->rsp[4]);
	widgets->humid = ((widgets->rsp[5]<<8)|widgets->rsp[6]);	
	    free(widgets->rsp);
	    modbus_close(ctx);
	    modbus_free(ctx);
        return 0;
}

/**************normal clock **********/
gboolean clock_timer(app_widgets *widgets)
{
    GDateTime *date_time;
    gchar *dmy_format;
    gchar *dt_format;
    
    date_time = g_date_time_new_now_local();
    dmy_format = g_date_time_format(date_time, "%d %b %y");
    dt_format = g_date_time_format(date_time, "%H:%M:%S");
    gtk_label_set_text(GTK_LABEL(widgets->lbl_date), dmy_format);
    gtk_label_set_text(GTK_LABEL(widgets->lbl_time), dt_format);
    g_free(dt_format);
    g_free(dmy_format);
    return TRUE;
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

/************************************
 * handler when pin 15 is pulled up
 * **********************************/
gboolean display_dry_contact(app_widgets *widgets)
{
    g_mutex_lock(&mutex_lock_2);
    widgets->data = 2;
    gchar *text2 = g_strdup_printf("%d", widgets->data);
    //gtk_label_set_text(GTK_LABEL(widgets->contact_lbl),text2);
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
    //gtk_label_set_text(GTK_LABEL(widgets->contact_lbl),text2);
    g_free(text2);
    g_mutex_unlock(&mutex_lock_2);
    return TRUE;
}

//this function is one of the main thread to check for dry contact, normally pin 15 is pulled up
//, if it's pulled down to GND then other function will be called
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

void display(app_widgets *widgets)
{
    g_mutex_lock(&mutex_lock_3);
    gchar *temp = g_strdup_printf("%.1f", (float)(widgets->temp)/100);
    gchar *humid = g_strdup_printf("%.1f", (float)(widgets->humid)/100);
    gtk_label_set_text(GTK_LABEL(widgets->lbl_real_temp), temp);
    gtk_label_set_text(GTK_LABEL(widgets->lbl_real_hu), humid);
    gtk_label_set_text(GTK_LABEL(widgets->lbl_temp), temp);
    gtk_label_set_text(GTK_LABEL(widgets->lbl_hu), humid);
    
    g_free(temp);
    g_free(humid);
    g_mutex_unlock(&mutex_lock_3);
    
    }
void on_btn1_clicked(GtkButton *button, app_widgets *widgets)
{
    g_thread_new(NULL, (GThreadFunc)relay1_control, (app_widgets*) widgets);
    }
//waiting    
void on_btn2_clicked(GtkButton *button, app_widgets *widgets)
{
    
    }
//waiting    
void on_btn3_clicked(GtkButton *button, app_widgets *widgets)
{
    
    }
//waiting    
void on_btn4_clicked(GtkButton *button, app_widgets *widgets)
{
    
    }
    
void on_btn_set_clicked(GtkButton *button, app_widgets *widgets)
{
    //set countdown timer for operation and anethesia
    widgets->op_hrs = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->hrs_op_in));
    widgets->op_mnt = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->mnt_op_in));
    widgets->op_sec = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->sec_op_in));
    widgets->an_hrs = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->hrs_an_in));
    widgets->an_mnt = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->mnt_an_in));
    widgets->an_sec = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->sec_an_in));
    //set temperature and humidity value
    widgets->adj_temp = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->spin_temp));
    widgets->adj_hu = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->spin_hu));
    //set count down clock next page
    g_timeout_add_seconds(1, (GSourceFunc)clock_timer, widgets);
    //g_timeout_add_seconds(1, (GSourceFunc)read_modbus_sensor, widgets);
    }

void on_btn_run_clicked(GtkButton *button, app_widgets *widgets)
{
        gtk_stack_set_visible_child_name(widgets->stack, "Run");
	//format operation time
	gchar *op_hrs = g_strdup_printf("%02d", widgets->op_hrs);
	gchar *op_mnt = g_strdup_printf("%02d", widgets->op_mnt);
	gchar *op_sec = g_strdup_printf("%02d", widgets->op_sec);
	//format anethesia time
	gchar *an_hrs = g_strdup_printf("%02d", widgets->an_hrs);
	gchar *an_mnt = g_strdup_printf("%02d", widgets->an_mnt);
	gchar *an_sec = g_strdup_printf("%02d", widgets->an_sec);
	//set operation time in run display
	gtk_label_set_text(GTK_LABEL(widgets->lbl_op_hrs), op_hrs);
	gtk_label_set_text(GTK_LABEL(widgets->lbl_op_mnt), op_mnt);
	gtk_label_set_text(GTK_LABEL(widgets->lbl_op_sec), op_sec);
	//set anethesia time in run display 
	gtk_label_set_text(GTK_LABEL(widgets->lbl_an_hrs), an_hrs);
	gtk_label_set_text(GTK_LABEL(widgets->lbl_an_mnt), an_mnt);
	gtk_label_set_text(GTK_LABEL(widgets->lbl_an_sec), an_sec);
	//free variable
	g_free(op_hrs);
	g_free(op_mnt);
	g_free(op_sec);
	g_free(an_hrs);
	g_free(an_mnt);
	g_free(an_sec);
    }

void on_btn_reset_clicked(GtkButton *button, app_widgets *widgets)
{
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->hrs_op_in), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->mnt_op_in), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->sec_op_in), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->hrs_an_in), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->mnt_an_in), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets->sec_an_in), 0);
    }
    
void on_btn_shut_clicked(GtkButton *button, app_widgets *widgets)
{
    gtk_main_quit();
    }
    

//page 1
//callback function to countdown the operation clock
gboolean op_countdown(app_widgets *widgets)
{
    widgets->op_sec--;
    if(widgets->op_sec < 0) 
        {
            widgets->op_sec = 59;
            widgets->op_mnt--;
            if(widgets->op_mnt < 0)
            {
                widgets->op_mnt = 59;
                widgets->op_hrs--;
                }
            }
    gchar *op_hrs = g_strdup_printf("%02d", widgets->op_hrs);
    gchar *op_mnt = g_strdup_printf("%02d", widgets->op_mnt);
    gchar *op_sec = g_strdup_printf("%02d", widgets->op_sec);
    
    gtk_label_set_text(GTK_LABEL(widgets->lbl_op_hrs), op_hrs);
    gtk_label_set_text(GTK_LABEL(widgets->lbl_op_mnt), op_mnt);
    gtk_label_set_text(GTK_LABEL(widgets->lbl_op_sec), op_sec);
    
    g_free(op_hrs);
    g_free(op_mnt);
    g_free(op_sec);
    if((widgets->op_hrs <= 0)&&(widgets->op_mnt <= 0) && (widgets->op_sec <= 0)) {return 0;}
    else return 1;
    }
    
void on_btn_op_start_clicked(GtkButton *button, app_widgets *widgets)
{
    g_timeout_add_seconds(1, (GSourceFunc)op_countdown, widgets);
    }

//callback function to countdown the anethesia clock
gboolean an_countdown(app_widgets *widgets)
{
    widgets->an_sec--;
    if(widgets->an_sec < 0) 
        {
            widgets->an_sec = 59;
            widgets->an_mnt--;
            if(widgets->an_mnt < 0)
            {
                widgets->an_mnt = 59;
                widgets->an_hrs--;
                }
            }
    gchar *an_hrs = g_strdup_printf("%02d", widgets->an_hrs);
    gchar *an_mnt = g_strdup_printf("%02d", widgets->an_mnt);
    gchar *an_sec = g_strdup_printf("%02d", widgets->an_sec);
    
    gtk_label_set_text(GTK_LABEL(widgets->lbl_an_hrs), an_hrs);
    gtk_label_set_text(GTK_LABEL(widgets->lbl_an_mnt), an_mnt);
    gtk_label_set_text(GTK_LABEL(widgets->lbl_an_sec), an_sec);
    
    g_free(an_hrs);
    g_free(an_mnt);
    g_free(an_sec);
    if((widgets->an_hrs < 0) || (widgets->an_mnt < 0) || (widgets->an_sec < 0)) {return 0;}
    else return 1;
    }
    
void on_btn_an_start_clicked(GtkButton *button, app_widgets *widgets)
{
    g_timeout_add_seconds(1, (GSourceFunc)an_countdown, widgets);
    }

void on_btn_back_clicked(GtkButton *button, app_widgets *widgets)
{
    gtk_stack_set_visible_child_name(widgets->stack, "Setup");
    }
    
/***********read 4-20mA value***********/
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
            uint16_t d=0;
            do{
                writeReg(fd, 0x08);
                d = readReg(fd);
                } while (d & 0x80);
                
            //read the data reg
            writeReg(fd, 0x38);
            widgets->adc_val = readData(fd) - 0x8000;
            printf("data = %d \n", widgets->adc_val);
            }
        close(fd);
}
    
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
    
    widgets->stack = GTK_STACK(gtk_builder_get_object(builder, "stk"));
    //page 0
    widgets->hrs_op_in = GTK_WIDGET(gtk_builder_get_object(builder, "hrs_op_in"));
    widgets->mnt_op_in = GTK_WIDGET(gtk_builder_get_object(builder, "mnt_op_in"));
    widgets->sec_op_in = GTK_WIDGET(gtk_builder_get_object(builder, "sec_op_in"));
    widgets->hrs_an_in = GTK_WIDGET(gtk_builder_get_object(builder, "hrs_an_in"));
    widgets->mnt_an_in = GTK_WIDGET(gtk_builder_get_object(builder, "mnt_an_in"));
    widgets->sec_an_in = GTK_WIDGET(gtk_builder_get_object(builder, "sec_an_in"));
    
    widgets->btn_set = GTK_WIDGET(gtk_builder_get_object(builder, "btn_set"));
    widgets->btn_reset = GTK_WIDGET(gtk_builder_get_object(builder, "btn_reset"));
    widgets->btn_shut = GTK_WIDGET(gtk_builder_get_object(builder, "btn_shut"));
    widgets->btn_run = GTK_WIDGET(gtk_builder_get_object(builder, "btn_run"));
    
    widgets->btn1 = GTK_WIDGET(gtk_builder_get_object(builder, "btn1"));
    widgets->btn2 = GTK_WIDGET(gtk_builder_get_object(builder, "btn2"));
    widgets->btn3 = GTK_WIDGET(gtk_builder_get_object(builder, "btn3"));
    widgets->btn4 = GTK_WIDGET(gtk_builder_get_object(builder, "btn4"));
    
    widgets->spin_temp = GTK_WIDGET(gtk_builder_get_object(builder, "spin_temp"));
    widgets->spin_hu = GTK_WIDGET(gtk_builder_get_object(builder, "spin_hu"));
    
    widgets->lbl_real_temp = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_real_temp"));
    widgets->lbl_real_hu = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_real_hu"));
    
    //page 1
    widgets->btn_op_start = GTK_WIDGET(gtk_builder_get_object(builder, "btn_op_start"));
    widgets->btn_an_start = GTK_WIDGET(gtk_builder_get_object(builder, "btn_an_start"));
    
    widgets->lbl_op_hrs = GTK_WIDGET(gtk_builder_get_object(builder, "run_op_hrs"));
    widgets->lbl_op_mnt = GTK_WIDGET(gtk_builder_get_object(builder, "run_op_mnt"));
    widgets->lbl_op_sec = GTK_WIDGET(gtk_builder_get_object(builder, "run_op_sec"));
    widgets->lbl_an_hrs = GTK_WIDGET(gtk_builder_get_object(builder, "run_an_hrs"));
    widgets->lbl_an_mnt = GTK_WIDGET(gtk_builder_get_object(builder, "run_an_mnt"));
    widgets->lbl_an_sec = GTK_WIDGET(gtk_builder_get_object(builder, "run_an_sec"));
    
    widgets->lbl_date = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_date"));
    widgets->lbl_time = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_time"));
    widgets->lbl_temp = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_temp"));
    widgets->lbl_hu = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_hu"));
    
    widgets->btn_run_back = GTK_WIDGET(gtk_builder_get_object(builder, "btn_back"));
    widgets->btn_run_reset = GTK_WIDGET(gtk_builder_get_object(builder, "btn_reset_2"));
    widgets->btn_run_shut = GTK_WIDGET(gtk_builder_get_object(builder, "btn_shut_2"));
    
    gtk_builder_connect_signals(builder, widgets);
    g_object_unref(builder);
    
    g_timeout_add_seconds(1, (GSourceFunc)display, widgets);
    
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


/***********spi read / no touch*************/
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

/*void on_btn_count_clicked(GtkButton *button, app_widgets *widgets)
{
    g_timeout_add_seconds(1, (GSourceFunc)countdown, widgets);
}*/
/*
gboolean timer_handler(app_widgets *widgets)
{
    widgets->data--;
    gchar *time = g_strdup_printf("%d", widgets->data);
    //gtk_label_set_text(GTK_LABEL(widgets->timer_lbl), time);
    g_free(time);
    if(widgets->data > 0)
    {return 1;}
    else return 0;
}*/
/*
void on_btn_light_clicked(GtkButton *button, app_widgets *widgets)
{
        g_thread_new(NULL, (GThreadFunc)relay1_control, (app_widgets*) widgets);
}
*/
/*//callback function for read button to initialize the clock timer
void on_btn_read_clicked(GtkButton *button, app_widgets *widgets)
{
    widgets->hrs = atoi(gtk_entry_get_text(GTK_ENTRY(widgets->input_hrs)));
    widgets->mnt = atoi(gtk_entry_get_text(GTK_ENTRY(widgets->input_mnt)));
    widgets->sec = atoi(gtk_entry_get_text(GTK_ENTRY(widgets->input_sec)));
    gchar *test = g_strdup_printf("%d : %d : %d", widgets->hrs, widgets->mnt, widgets->sec);
    gtk_label_set_text(GTK_LABEL(widgets->timer_lbl), test);
    g_free(test);
}*/
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
