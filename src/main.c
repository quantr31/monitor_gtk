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

//#include <stdint.h>
//#include <unistd.h>
//#include <stdio.h>
//#include <time.h>
//#include <string.h>
//#include <sys/types.h> // open
//#include <sys/stat.h>  // open
//#include <inttypes.h>  // uint8_t, etc
#include <stdlib.h>
#include <errno.h>
//system access
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/types.h>
#include <linux/i2c-dev.h>
//external lib
#include <gtk/gtk.h>
#include <bcm2835.h>
#include <glib.h>
#include <X11/Xlib.h>
#include <modbus.h>

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
    //spin button for countdown timer 
    GtkWidget *hrs_op_in;
    GtkWidget *mnt_op_in;
    GtkWidget *sec_op_in;
    GtkWidget *hrs_an_in;
    GtkWidget *mnt_an_in;
    GtkWidget *sec_an_in;
    //button 
    GtkWidget *btn_run;
    GtkWidget *btn_reset;
    GtkWidget *btn_shut;
    //light switch 
    GtkWidget *sw_light1;
    GtkWidget *sw_light2;
    GtkWidget *sw_uv;
    //change temp and humid
    GtkWidget *img_temp;
    GtkWidget *img_hu;
    GtkWidget *spin_temp;
    GtkWidget *spin_hu;
    GtkWidget *lbl_real_temp;
    GtkWidget *lbl_real_hu;
    //status display 
    GtkWidget *img_fan;
    GtkWidget *img_heater;
    GtkWidget *img_filter;
    GtkWidget *img_uv;
    
    GtkWidget *img_light1;
    GtkWidget *img_light2;
    GtkWidget *img_lightuv;
    
    GtkWidget *img_o2;
    GtkWidget *img_vac;
    GtkWidget *img_co2;
    GtkWidget *img_agss;
    GtkWidget *img_n2o;
    
    //btn picture
    GtkWidget *img_play;
    GtkWidget *img_reset;
    GtkWidget *img_shut;
    
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
    GtkWidget *lbl_pre;
    
    GtkWidget *img_temp1;
    GtkWidget *img_hu1;
    GtkWidget *img_pre;
    GtkWidget *img_back;
    GtkWidget *img_reset1;
    GtkWidget *img_shut1;
    GtkWidget *img_run_op;
    GtkWidget *img_run_an;
    
    GtkWidget *btn_run_back;
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
    float adc_val;
    //adjust temp&humidity
    uint8_t adj_temp;
    uint8_t adj_hu;
} app_widgets;

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
    while(1){   
    req_length = modbus_send_raw_request(ctx, req, 8*sizeof(uint8_t));
    if(req_length < 0) {printf("read failed :(\n");}
	modbus_receive_confirmation(ctx, widgets->rsp);
	widgets->temp = ((widgets->rsp[3]<<8)|widgets->rsp[4]);
	widgets->humid = ((widgets->rsp[5]<<8)|widgets->rsp[6]);	
    }
	    free(widgets->rsp);
	    modbus_close(ctx);
	    modbus_free(ctx);
        return 0;
}

void ADCread(app_widgets *widgets)
{
    int fd;
    uint8_t ads_addr = 0x48;
    uint8_t *buf;
    buf = (uint8_t*)malloc(sizeof(uint8_t)*10);
    fd = open("/dev/i2c-1", O_RDWR);
    ioctl(fd, I2C_SLAVE, ads_addr);
    // open device on /dev/i2c-1 the default on Raspberry Pi B
    /*if ((fd = open("/dev/i2c-1", O_RDWR)) < 0) {
	printf("Error: Couldn't open device! %d\n", fd);
	exit(-1);
    }
    // connect to ads1115 as i2c slave
    if (ioctl(fd, I2C_SLAVE, ads_addr) < 0) {
	printf("Error: Couldn't find device on address!\n");
	exit(-1);
    }*/
    //conversion
    while(1){
	buf[0] = 1;
	buf[1] = 0xc3;
	buf[2] = 0x05;
      if (write(fd, buf, 3) != 3) {
	perror("Write to register 1");
	exit(-1);
	}
      do{
	if (read(fd, buf, 2) != 2) {
	  perror("Read conversion");
	  exit(-1);
	}
	} while ((buf[0] & 0x80) == 0);
	buf[0] = 0;   // conversion register is 0
      if (write(fd, buf, 1) != 1) {
	perror("Write register select");
	exit(-1);
	}
      if (read(fd, buf, 2) != 2) {
	perror("Read conversion");
	exit(-1);
	}
      widgets->adc_val = (float)(((int16_t)buf[0]*256 + (uint16_t)buf[1])*4.096/32768.0);
      printf("%f \n", widgets->adc_val);
	}
	close(fd);
	free(buf);
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
    g_date_time_unref(date_time);
    return TRUE;
    }

void relay1_control(app_widgets *widgets)
{
      bcm2835_gpio_fsel(PIN_OUT, BCM2835_GPIO_FSEL_OUTP);
      while(1)
      {
            bcm2835_gpio_write(PIN_OUT, HIGH);
            //delay(500);
            //bcm2835_gpio_write(PIN_OUT, LOW);
            //delay(500);
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
    //widgets->data = 3;
    //gchar *text2 = g_strdup_printf("%d", widgets->data);
    //gtk_label_set_text(GTK_LABEL(widgets->contact_lbl),text2);
    //g_free(text2);
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
    //bcm2835_close();
}

void display(app_widgets *widgets)
{
    g_mutex_lock(&mutex_lock_3);
    gchar *temp = g_strdup_printf("%.1f°C", (float)(widgets->temp)/100);
    gchar *humid = g_strdup_printf("%.1f %%", (float)(widgets->humid)/100);
    
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

void on_btn_run_clicked(GtkButton *button, app_widgets *widgets)
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
    gtk_stack_set_visible_child_name(widgets->stack, "Run");
	
        //format operation time
	gchar *op_hrs = g_strdup_printf("%02d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->hrs_op_in)));
	gchar *op_mnt = g_strdup_printf("%02d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->mnt_op_in)));
	gchar *op_sec = g_strdup_printf("%02d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->sec_op_in)));
	//format anethesia time
	gchar *an_hrs = g_strdup_printf("%02d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->hrs_an_in)));
	gchar *an_mnt = g_strdup_printf("%02d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->mnt_an_in)));
	gchar *an_sec = g_strdup_printf("%02d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widgets->sec_an_in)));
	//set operation time in run display
	gtk_label_set_text(GTK_LABEL(widgets->lbl_op_hrs), op_hrs);
	gtk_label_set_text(GTK_LABEL(widgets->lbl_op_mnt), op_mnt);
	gtk_label_set_text(GTK_LABEL(widgets->lbl_op_sec), op_sec);
	//set anethesia time in run display 
	gtk_label_set_text(GTK_LABEL(widgets->lbl_an_hrs), an_hrs);
	gtk_label_set_text(GTK_LABEL(widgets->lbl_an_mnt), an_mnt);
	gtk_label_set_text(GTK_LABEL(widgets->lbl_an_sec), an_sec);
    
/*    //format operation time
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
    * */
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
    if((widgets->op_hrs <= 0)&&(widgets->op_mnt <= 0)&&(widgets->op_sec <= 0)) {return 0;}
    else {
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
    
    return 1;
        }
    }
    
void on_btn_op_start_clicked(GtkButton *button, app_widgets *widgets)
{
    g_timeout_add_seconds(1, (GSourceFunc)op_countdown, widgets);
    }

//callback function to countdown the anethesia clock
gboolean an_countdown(app_widgets *widgets)
{
    if((widgets->an_hrs <= 0) && (widgets->an_mnt <= 0) && (widgets->an_sec <= 0)) {return 0;}
    else{
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
    
    return 1;
        }
    }
    
void on_btn_an_start_clicked(GtkButton *button, app_widgets *widgets)
{
    g_timeout_add_seconds(1, (GSourceFunc)an_countdown, widgets);
    }

void on_btn_back_clicked(GtkButton *button, app_widgets *widgets)
{
    gtk_stack_set_visible_child_name(widgets->stack, "Setup");
    }
    
void myCSS(void){
    GtkCssProvider *provider;
    GdkDisplay *display;
    GdkScreen *screen;
    
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(provider, "/home/pi/Desktop/project/src/style.css", NULL);
    display = gdk_display_get_default();
    screen = gdk_display_get_default_screen(display);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    }
    
int main(int argc, char *argv[])
{    
    //init glade 
    GtkBuilder      *builder; 
    GtkWidget       *window;
    app_widgets *widgets = g_slice_new(app_widgets);
    GThread *thread1;
    GThread *thread2;
    GThread *thread3;
    
    //init bcm2835 lib
    if(!bcm2835_init())
    return 1;
    //thread to read ADC via SPI
    //thread1 = g_thread_new(NULL, (GThreadFunc)readADC, (app_widgets*) widgets);
    thread1 = g_thread_new(NULL, (GThreadFunc)ADCread, (app_widgets*) widgets);
    //thread to check dry contact
    thread2 = g_thread_new(NULL, (GThreadFunc)check_dry_contact, (app_widgets*) widgets);
    //thread to read modbus sensor 
    thread3 = g_thread_new(NULL, (GThreadFunc)read_modbus_sensor, (app_widgets*) widgets);
    
    XInitThreads();
    gtk_init(&argc, &argv);
    myCSS();
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
    
    //widgets->btn_set = GTK_WIDGET(gtk_builder_get_object(builder, "btn_set"));
    widgets->btn_reset = GTK_WIDGET(gtk_builder_get_object(builder, "btn_reset"));
    widgets->btn_shut = GTK_WIDGET(gtk_builder_get_object(builder, "btn_shut"));
    widgets->btn_run = GTK_WIDGET(gtk_builder_get_object(builder, "btn_run"));
    
    widgets->sw_light1 = GTK_WIDGET(gtk_builder_get_object(builder, "sw_light1"));
    widgets->sw_light2 = GTK_WIDGET(gtk_builder_get_object(builder, "sw_light2"));
    widgets->sw_uv = GTK_WIDGET(gtk_builder_get_object(builder, "sw_uv"));
    
    widgets->img_fan = GTK_WIDGET(gtk_builder_get_object(builder, "img_fan"));
    widgets->img_heater = GTK_WIDGET(gtk_builder_get_object(builder, "img_heater"));
    widgets->img_filter = GTK_WIDGET(gtk_builder_get_object(builder, "img_filter"));
    widgets->img_uv = GTK_WIDGET(gtk_builder_get_object(builder, "img_uv"));
    
    widgets->img_o2 = GTK_WIDGET(gtk_builder_get_object(builder, "img_o2"));
    widgets->img_vac = GTK_WIDGET(gtk_builder_get_object(builder, "img_vac"));
    widgets->img_co2 = GTK_WIDGET(gtk_builder_get_object(builder, "img_co2"));
    widgets->img_agss = GTK_WIDGET(gtk_builder_get_object(builder, "img_agss"));
    widgets->img_n2o = GTK_WIDGET(gtk_builder_get_object(builder, "img_n2o"));
    
    widgets->img_light1 = GTK_WIDGET(gtk_builder_get_object(builder, "img_light1"));
    widgets->img_light2 = GTK_WIDGET(gtk_builder_get_object(builder, "img_light2"));
    widgets->img_lightuv = GTK_WIDGET(gtk_builder_get_object(builder, "img_lightuv"));
    
    widgets->img_temp = GTK_WIDGET(gtk_builder_get_object(builder, "img_temp"));
    widgets->img_hu = GTK_WIDGET(gtk_builder_get_object(builder, "img_hu"));
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
    widgets->lbl_pre = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_pre"));
    
    widgets->btn_run_back = GTK_WIDGET(gtk_builder_get_object(builder, "btn_run_back"));
    widgets->btn_run_shut = GTK_WIDGET(gtk_builder_get_object(builder, "btn_run_shut"));
    //acquire button image
    widgets->img_play = gtk_image_new_from_file("src/image/play.png");
    widgets->img_reset = gtk_image_new_from_file("src/image/reset.png");
    widgets->img_shut = gtk_image_new_from_file("src/image/shut.png");
    widgets->img_back = gtk_image_new_from_file("src/image/back.png");
    widgets->img_shut1 = gtk_image_new_from_file("src/image/shut1.png");
    widgets->img_run_op = gtk_image_new_from_file("src/image/play1.png");
    widgets->img_run_an = gtk_image_new_from_file("src/image/play2.png");
    
    gtk_builder_connect_signals(builder, widgets);
    g_object_unref(builder);
    
    g_timeout_add_seconds(1, (GSourceFunc)display, widgets);
    //set picture
    //page 0
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_fan), "src/image/fanon.png");
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_heater), "src/image/heaton.png");
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_filter), "src/image/filton.png");
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_uv), "src/image/uvoff.png");

    gtk_image_set_from_file(GTK_IMAGE(widgets->img_o2), "src/image/o2off.png");
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_vac), "src/image/vacoff.png");
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_co2), "src/image/co2off.png");
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_agss), "src/image/agssoff.png");
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_n2o), "src/image/n2off.png");
    
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_light1), "src/image/light.png");
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_light2), "src/image/light.png");
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_lightuv), "src/image/uv.png");
    
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_temp), "src/image/temp.png");
    gtk_image_set_from_file(GTK_IMAGE(widgets->img_hu), "src/image/humid.png");
    
    gtk_button_set_image(GTK_BUTTON(widgets->btn_run), GTK_WIDGET(widgets->img_play));
    gtk_button_set_image(GTK_BUTTON(widgets->btn_reset), GTK_WIDGET(widgets->img_reset));
    gtk_button_set_image(GTK_BUTTON(widgets->btn_shut), GTK_WIDGET(widgets->img_shut));
    
    //page 1
    gtk_button_set_image(GTK_BUTTON(widgets->btn_run_back), GTK_WIDGET(widgets->img_back));
    gtk_button_set_image(GTK_BUTTON(widgets->btn_run_shut), GTK_WIDGET(widgets->img_shut1));
    
    gtk_button_set_image(GTK_BUTTON(widgets->btn_op_start), GTK_WIDGET(widgets->img_run_op));
    gtk_button_set_image(GTK_BUTTON(widgets->btn_an_start), GTK_WIDGET(widgets->img_run_an));
    gtk_widget_show(window);

    gtk_main();
    g_thread_unref(thread1);
    g_thread_unref(thread2);
    g_thread_unref(thread3);
    g_slice_free(app_widgets, widgets);
    return 0;
}

// called when window is closed
void on_window_main_destroy()
{
    gtk_main_quit();
}

/*    
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
*/

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
