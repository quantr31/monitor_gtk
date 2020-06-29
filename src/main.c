#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <bcm2835.h>

#define PIN RPI_GPIO_P1_11

typedef struct {
    GtkWidget *timer_lbl;
    GtkWidget *clock_lbl;
    GtkWidget *input_hrs;
    GtkWidget *input_mnt;
    GtkWidget *input_sec;
    signed int hrs;
    signed int mnt;
    signed int sec;
    signed int data;
} app_widgets;

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

//callback function for read button
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
    //g_timeout_add_seconds(1, (GSourceFunc)timer_handler, widgets);
    g_timeout_add_seconds(1, (GSourceFunc)countdown, widgets);
}

int main(int argc, char *argv[])
{    
    //init glade 
    GtkBuilder      *builder; 
    GtkWidget       *window;
    app_widgets *widgets = gsudo _slice_new(app_widgets);
    
    gtk_init(&argc, &argv);

    builder = gtk_builder_new_from_file("glade/window_main.glade");

    window = GTK_WIDGET(gtk_builder_get_object(builder, "window_main"));
    gtk_widget_set_size_request(GTK_WIDGET(window), 1920, 1080);
    
    widgets->input_hrs = GTK_WIDGET(gtk_builder_get_object(builder, "entry_input_hrs"));
    widgets->input_mnt = GTK_WIDGET(gtk_builder_get_object(builder, "entry_input_mnt"));
    widgets->input_sec = GTK_WIDGET(gtk_builder_get_object(builder, "entry_input_sec"));
    widgets->timer_lbl = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_text"));
    widgets->clock_lbl = GTK_WIDGET(gtk_builder_get_object(builder, "lbl_clock"));
    //widgets->data = 60;

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







