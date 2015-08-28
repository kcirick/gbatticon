
#define SYSFS_PATH "/sys/class/power_supply"
#define DEFAULT_TIMEOUT 3000        // in milliseconds 
#define TIMEOUT_NEVER   0

static gint       update_interval      = 5;
static gint       low_level            = 20;
static gint       critical_level       = 5;

static gchar*     left_click_command   = NULL;

static gboolean   hide_notification    = FALSE;

