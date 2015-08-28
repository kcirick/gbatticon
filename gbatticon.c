/*
 * gbatticon.c
 */

#include <stdlib.h>
#include <math.h>

#include <gtk/gtk.h>
#include <glib/gprintf.h>

#include "config.h"

//-- Definitions -----
#define COLOUR_RED     "\x1b[31m"
#define COLOUR_GREEN   "\x1b[32m"
#define COLOUR_YELLOW  "\x1b[33m"
#define COLOUR_PURPLE  "\x1b[35m"
#define COLOUR_RESET   "\x1b[0m"

//-- Enums -----
enum { DEBUG, INFO, WARNING, ERROR, FATAL };

enum { MISSING = 0,
   UNKNOWN,
   CHARGED,
   CHARGING,
   DISCHARGING,
   NOT_CHARGING,
   LOW_LEVEL,
   CRITICAL_LEVEL };

//-- Variables -----
static gchar* battery_path = NULL;
static gchar* ac_path      = NULL;

static gboolean battery_present  = FALSE;
static gboolean ac_online        = FALSE;

//-- Function declarations -----
static void say(gint, gchar*, gint);

static gboolean get_power_supply(gchar*, gboolean);
static gboolean get_battery_full_capacity (gboolean*, gdouble*);
static gboolean get_battery_remaining_capacity (gboolean, gdouble*);
static gboolean get_battery_charge(gboolean, gint*);
static gboolean get_battery_status(gint*);
static gboolean get_ac_status(gboolean*);

static gboolean get_sysattr_string(gchar*, gchar*, gchar**);
static gboolean get_sysattr_double(gchar*, gchar*, gdouble*);

static void init_tray_icon();
static void init_ac_battery(gchar*);
static void update_tray_icon(GtkStatusIcon*);
static void tray_icon_click(GtkStatusIcon*, gpointer); 

static void notify_msg(gchar*, gchar*, gint);

static gchar* get_battery_string (gint, gint);
static gchar* get_icon_name (gint, gint);

//------------------------------------------------------------------------
// Functions
//-----------
static void say(gint level, gchar* message, gint exit_type){
   gchar* coloured_type;
   if(level==ERROR || level==FATAL)   
      coloured_type=g_strdup(COLOUR_RED "ERROR" COLOUR_RESET);
   else if(level==WARNING)
      coloured_type=g_strdup(COLOUR_YELLOW "WARNING" COLOUR_RESET);
   else if(level==DEBUG)
      coloured_type=g_strdup(COLOUR_PURPLE "DEBUG" COLOUR_RESET);
   else
      coloured_type=g_strdup(COLOUR_GREEN "INFO" COLOUR_RESET);

   g_printf("%s [%s]:\t%s\n", NAME, coloured_type, message);

   if(exit_type==EXIT_SUCCESS || exit_type==EXIT_FAILURE ) 
      exit(exit_type);
}

static gboolean get_power_supply(gchar* battery_id, gboolean print_list){
   GError *error = NULL;
   gchar* sysattr_value;

   if(print_list) say(INFO, "List of available power supplies:", -1);

   GDir *directory = g_dir_open (SYSFS_PATH, 0, &error);
   if(!directory) say(ERROR, error->message, EXIT_FAILURE);

   const gchar *file = g_dir_read_name(directory);
   while(file){
      gchar* path = g_build_filename(SYSFS_PATH, file, NULL);

      if(!get_sysattr_string(path, "type", &sysattr_value)) continue;

      //process battery
      if(g_str_has_prefix(sysattr_value, "Battery")){
         if(print_list){
            gchar *ps_id = g_path_get_basename(path);
            say(INFO, g_strdup_printf("type: Battery\tid: %s\tpath: %s", ps_id, path), -1);
            g_free(ps_id);
         }

         if(!battery_path && (!battery_id || g_str_has_suffix(path, battery_id)))
            battery_path = g_strdup(path);
      }
      //process AC
      if(g_str_has_prefix(sysattr_value, "Mains")){
         if(print_list){
            gchar *ps_id = g_path_get_basename(path);
            say(INFO, g_strdup_printf("type: AC\tid: %s\tpath: %s", ps_id, path), -1);
            g_free(ps_id);
         }

         if(!ac_path)   ac_path = g_strdup(path);
      }
      g_free(sysattr_value);
      g_free(path);
      file = g_dir_read_name(directory);
   }
   g_dir_close(directory);

   if(print_list) exit(EXIT_SUCCESS);

   return TRUE;
}

static gboolean get_battery_full_capacity(gboolean *use_charge, gdouble *capacity){
   gboolean sysattr_status;

   g_return_val_if_fail(use_charge, FALSE);
   g_return_val_if_fail(capacity, FALSE);
   
   sysattr_status = get_sysattr_double(battery_path, "energy_full", capacity);
   *use_charge = FALSE;

   if(!sysattr_status){
      sysattr_status = get_sysattr_double(battery_path, "charge_full", capacity);
      *use_charge = TRUE;
   }
   return sysattr_status;
}

static gboolean get_battery_remaining_capacity(gboolean use_charge, gdouble *capacity){
   g_return_val_if_fail(capacity, FALSE);

   return get_sysattr_double(battery_path, use_charge ? "charge_now" : "energy_now", capacity);
}

static gboolean get_battery_charge (gboolean remaining, gint *percentage) {
   gdouble full_capacity, remaining_capacity;
   gboolean use_charge;
   
   if(!get_battery_full_capacity(&use_charge, &full_capacity))          return FALSE;
   if(!get_battery_remaining_capacity(use_charge, &remaining_capacity)) return FALSE;

   *percentage = (gint)fmin(floor(remaining_capacity / full_capacity *100.0), 100.0);

   return TRUE;
}

static gboolean get_battery_status (gint *status) {
   gchar *sysattr_value;

   g_return_val_if_fail(status, FALSE);

   if(!get_sysattr_string(battery_path, "status", &sysattr_value)) return FALSE;

        if(g_str_has_prefix(sysattr_value, "Charging"))     *status = CHARGING;
   else if(g_str_has_prefix(sysattr_value, "Discharging"))  *status = DISCHARGING;
   else if(g_str_has_prefix(sysattr_value, "Not charging")) *status = NOT_CHARGING;
   else if(g_str_has_prefix(sysattr_value, "Full"))         *status = CHARGED;
   else                                                     *status = UNKNOWN;

   g_free(sysattr_value);
   return TRUE;
}

static gboolean get_ac_status(gboolean *online){
   gchar *sysattr_value;

   g_return_val_if_fail(online, FALSE);
   g_return_val_if_fail(ac_path, FALSE);

   if(!get_sysattr_string(ac_path, "online", &sysattr_value)) return FALSE;
   
   *online = g_str_has_prefix(sysattr_value, "1") ? TRUE : FALSE;

   g_free(sysattr_value);
   return TRUE;
}

static gboolean get_sysattr_string(gchar *path, gchar *attribute, gchar **value) {
   gchar *sysattr_filename;
   gboolean sysattr_status;

   g_return_val_if_fail (path != NULL, FALSE);
   g_return_val_if_fail (attribute != NULL, FALSE);
   g_return_val_if_fail (value != NULL, FALSE);

   sysattr_filename = g_build_filename (path, attribute, NULL);
   sysattr_status = g_file_get_contents (sysattr_filename, value, NULL, NULL);
   g_free (sysattr_filename);

   return sysattr_status;
}

static gboolean get_sysattr_double (gchar *path, gchar *attribute, gdouble *value) {
   gchar *sysattr_filename, *sysattr_value;
   gboolean sysattr_status;

   g_return_val_if_fail (path != NULL, FALSE);
   g_return_val_if_fail (attribute != NULL, FALSE);

   sysattr_filename = g_build_filename (path, attribute, NULL);
   sysattr_status = g_file_get_contents (sysattr_filename, &sysattr_value, NULL, NULL);
   g_free (sysattr_filename);

   if (sysattr_status) {
      gdouble double_value = g_ascii_strtod (sysattr_value, NULL);

      if (double_value < 0.01)   sysattr_status = FALSE;
      if (value != NULL)         *value = double_value;

      g_free (sysattr_value);
   }

   return sysattr_status;
}

//-- Init function group -----
static void init_tray_icon(){
   GtkStatusIcon *tray_icon = gtk_status_icon_new();

   gtk_status_icon_set_tooltip_text(tray_icon, "gbatticon");
   gtk_status_icon_set_visible(tray_icon, TRUE);

   update_tray_icon(tray_icon);
   g_timeout_add_seconds(update_interval, (GSourceFunc)update_tray_icon, (gpointer)tray_icon);
   g_signal_connect(G_OBJECT(tray_icon), "activate", G_CALLBACK(tray_icon_click), NULL);

}


static void init_ac_battery(gchar* battery_id){

   if (!get_power_supply(battery_id, FALSE))
      say(ERROR, "Problem with power supply",  EXIT_FAILURE);

   if(!battery_path) notify_msg("AC only - no battery", NULL, DEFAULT_TIMEOUT);

   gchar* sysattr_value;
   if(!get_sysattr_string(battery_path, "present", &sysattr_value))
      say(ERROR, "Problem with battery", EXIT_FAILURE);

   battery_present = g_str_has_prefix(sysattr_value, "1") ? TRUE : FALSE;

   g_free(sysattr_value);
}

static void update_tray_icon(GtkStatusIcon *tray_icon){

   gint battery_status              = -1;
   static gint old_battery_status   = -1;

   static gboolean battery_low      = FALSE;
   static gboolean battery_critical = FALSE;

   gint percent;
   gchar *battery_str, *icon_name;

   g_return_if_fail(tray_icon);

   // AC only - no battery
   if(!battery_path){
      gtk_status_icon_set_tooltip_text(tray_icon, "AC only - no battery");
      gtk_status_icon_set_from_icon_name(tray_icon, "ac-adapter");
      
      return;
   }

   // Battery
   if(!battery_present) battery_status = MISSING;
   else {
      if(!get_battery_status(&battery_status)) return;

      // More to do if status is UNKNOWN
      if(battery_status==UNKNOWN && get_ac_status(&ac_online)){
         if(ac_online){
            battery_status = CHARGING;
            if(get_battery_charge(FALSE, &percent) && percent >=99)
               battery_status = CHARGED;
         } else
            battery_status = DISCHARGING;
      }
   }

#define UPDATE_STATUS(PCT,EXPIRES) \
   battery_str = get_battery_string(battery_status, PCT);      \
   icon_name   = get_icon_name(battery_status, PCT);           \
   \
   if(old_battery_status != battery_status){                   \
      old_battery_status = battery_status;                     \
      notify_msg(battery_str, icon_name, EXPIRES);         \
   }  \
   gtk_status_icon_set_tooltip_text(tray_icon, battery_str);   \
   gtk_status_icon_set_from_icon_name(tray_icon, icon_name);

   switch(battery_status){
      case MISSING:
         UPDATE_STATUS(0, TIMEOUT_NEVER);
         break;
      case UNKNOWN:
         UPDATE_STATUS(0, DEFAULT_TIMEOUT); 
         break;
      case CHARGED:
         UPDATE_STATUS(100, DEFAULT_TIMEOUT); 
         break;
      case CHARGING:
         if(!get_battery_charge(FALSE, &percent)) return;

         UPDATE_STATUS(percent, DEFAULT_TIMEOUT);
         break;
      case DISCHARGING:
      case NOT_CHARGING:
         if(!get_battery_charge(TRUE, &percent)) return;

         battery_str = get_battery_string(battery_status, percent);
         icon_name   = get_icon_name(battery_status, percent);

         if(old_battery_status != DISCHARGING){
            old_battery_status = DISCHARGING;
            notify_msg(battery_str, icon_name, DEFAULT_TIMEOUT);

            battery_low       = FALSE;
            battery_critical  = FALSE;
         }

         if(!battery_low && percent <= low_level){
            battery_low = TRUE;
            battery_str = get_battery_string(LOW_LEVEL, percent);
            notify_msg(battery_str, icon_name, TIMEOUT_NEVER);
         }

         if(!battery_critical && percent <= critical_level){
            battery_critical = TRUE;
            battery_str = get_battery_string(CRITICAL_LEVEL, percent);
            notify_msg(battery_str, icon_name, TIMEOUT_NEVER);
         }

         gtk_status_icon_set_tooltip_text(tray_icon, battery_str);
         gtk_status_icon_set_from_icon_name(tray_icon, icon_name);
         break;
   }
}

static void tray_icon_click(GtkStatusIcon *icon, gpointer user_data) {

   if(!left_click_command) return;

   GError *error = NULL;
   if(!g_spawn_command_line_async(left_click_command, &error)){
      say(ERROR, g_strdup_printf("Cannot execute command: %s", error->message), -1);
      g_error_free(error); error=NULL;
   }
}

static void notify_msg(gchar *text, gchar *icon, gint timeout) {
   g_return_if_fail(text);
   if(hide_notification) return;
   
   gchar* command = "notify-send";
   if(icon) command = g_strconcat(command, " -i ", icon, NULL);
   command = g_strdup_printf("%s -t %d '%s'", command, timeout, text);

   GError *error = NULL;
   if(!g_spawn_command_line_async(command, &error)){
      say(ERROR, g_strdup_printf("Cannot execute command: %s", error->message), -1);
      g_error_free(error); error=NULL;
   }
   g_free(command);
}

//-- get_string functions -----
static gchar* get_battery_string(gint state, gint percent){
   static gchar* battery_string;

   switch(state){
      case MISSING:
         battery_string = g_strdup("Battery is missing!");
         break;
      case UNKNOWN:
         battery_string = g_strdup("Status unknown!");
         break;
      case CHARGED:
         battery_string = g_strdup("Fully charged!");
         break;
      case CHARGING:
         battery_string = g_strdup_printf("Charging (%d%% remaining)", percent);
         break;
      case DISCHARGING:
         battery_string = g_strdup_printf("Discharging (%d%% remaining)", percent);
         break;
      case NOT_CHARGING:
         battery_string = g_strdup_printf("Not charging (%d%% remaining)", percent);
         break;
      case LOW_LEVEL:
         battery_string = g_strdup_printf("Level is low! (%d%% remaining)", percent);
         break;
      case CRITICAL_LEVEL:
         battery_string = g_strdup_printf("Level is critical! (%d%% remaining)", percent);
         break;
   }

   return battery_string;
}

static gchar* get_icon_name(gint state, gint percent){
   static gchar* icon_name;

   icon_name = g_strdup("battery");

   if(state==MISSING||state==UNKNOWN)
      icon_name = g_strconcat(icon_name, "-missing", NULL);
   else {
           if(percent <= 20)  icon_name = g_strconcat(icon_name, "-caution", NULL);
      else if(percent <= 40)  icon_name = g_strconcat(icon_name, "-low", NULL);
      else if(percent <= 80)  icon_name = g_strconcat(icon_name, "-good", NULL);
      else                    icon_name = g_strconcat(icon_name, "-full", NULL);
   
      if(state == CHARGING)      icon_name = g_strconcat(icon_name, "-charging", NULL);
      else if(state == CHARGED)  icon_name = g_strconcat(icon_name, "-charged", NULL);
   }

   return icon_name;
}

//------------------------------------------------------------------------
// Main function
//---------------
int main (int argc, char **argv) {

   static GError *error;
   static gboolean version     = FALSE;
   static gboolean list_ps     = FALSE;
   static GOptionEntry option_entries[] = {
      { "version"             , 'v', 0, G_OPTION_ARG_NONE , &version , "Print version", NULL},
      { "list-power-supplies" , 'l', 0, G_OPTION_ARG_NONE , &list_ps , "List available power supplies ", NULL},
      { NULL } };

   if (!gtk_init_with_args(&argc, &argv, "", option_entries, NULL, &error)) 
      say(ERROR, error->message, EXIT_FAILURE);

   if(!gtk_icon_theme_has_icon (gtk_icon_theme_get_default (), "battery-full"))
      say(ERROR, "No icon type found", EXIT_FAILURE);

   if (version)   say(INFO, "Version "VERSION, EXIT_SUCCESS);
   if (list_ps)   get_power_supply (NULL, TRUE);

   init_ac_battery(argc>1 ? argv[1]:NULL);
   init_tray_icon();

   gtk_main();

   return EXIT_SUCCESS;
}
