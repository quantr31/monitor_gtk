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

    signed int hrs;
    signed int mnt;
    signed int sec;
    signed int data;
} app_widgets;

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
    return 1
    ;
    //init GPIO thread
    
    //test thread
    //g_thread_new(NULL, (GThreadFunc)LED_control, (app_widgets*) widgets);
    g_thread_new(NULL, (GThreadFunc)check_dry_contact, (app_widgets*) widgets);
    
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
    //widgets->data = 60;

    gtk_builder_connect_signals(builder, widgets);
    g_object_unref(builder);

    gtk_widget_show(window);

    //enter thread
    gtk_main();
    g_slice_free(app_widgets, widgets);
    return 0;
}

// called when window is closed
void on_window_main_destroy()
{
    gtk_main_quit();
}







