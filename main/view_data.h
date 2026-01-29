#ifndef VIEW_DATA_H
#define VIEW_DATA_H

#include "config.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum start_screen {
    SCREEN_SENSECAP_LOG,
    SCREEN_WIFI_CONFIG,
    SCREEN_BUS_COUNTDOWN,
    SCREEN_TRAIN_STATION,
    SCREEN_SETTINGS,
};

#define WIFI_SCAN_LIST_SIZE  15
#define MAX_DEPARTURES 50
#define MAX_BUS_LINES 10
#define MAX_TRAIN_LINES 20
#define MAX_DIRECTIONS 5  // Maximum number of distinct directions to track

// WiFi structures
struct view_data_wifi_st {
    bool   is_connected;
    bool   is_connecting;
    bool   is_network;
    char   ssid[32];
    int8_t rssi;
};

struct view_data_wifi_config {
    char    ssid[32];
    uint8_t password[64];
    bool    have_password;
};

struct view_data_wifi_item {
    char   ssid[32];
    bool   auth_mode;
    int8_t rssi;
};

struct view_data_wifi_list {
    bool  is_connect;
    struct view_data_wifi_item  connect;
    uint16_t cnt;
    struct view_data_wifi_item aps[WIFI_SCAN_LIST_SIZE];
};

struct view_data_wifi_connet_ret_msg {
    uint8_t ret;
    char    msg[64];
};

/** Full network info for WiFi settings panel (IP, DNS, gateway, RSSI, etc.) */
struct view_data_network_info {
    char   ip[16];
    char   gateway[16];
    char   netmask[16];
    char   dns_primary[16];
    char   dns_secondary[16];
    char   ssid[32];
    int8_t rssi;
    bool   connected;
};

// Display settings
struct view_data_display {
    int   brightness; // 0~100
    bool  sleep_mode_en;
    int   sleep_mode_time_min;  // 0 = always on, 1, 5, 10, 30, 60 minutes
};

// Time configuration
struct view_data_time_cfg {
    bool    time_format_24;
    bool    auto_update;
    time_t  time;
    bool    set_time;
    bool    auto_update_zone;
    int8_t  zone;
    bool    daylight;
} __attribute__((packed));

// Bus departure structure
struct bus_departure_view {
    char line[16];              // Bus line number (e.g., "1", "4", "12")
    char destination[64];       // Final destination
    char time_str[6];           // Time as "HH:MM"
    time_t departure_timestamp; // Exact departure timestamp for sorting and recalculation
    int minutes_until;          // Minutes until departure
    int delay_minutes;          // Delay in minutes (negative = early, positive = late)
    int direction_index;        // Index in directions array
    bool valid;
    char journey_name[64];      // Unique journey reference
};

// Train departure structure
struct train_departure_view {
    char line[16];              // Train line (e.g., "S12", "IC3", "RE")
    char destination[64];       // Final destination
    char via[128];              // Intermediate stations
    char platform[16];          // Platform number (e.g., "1", "4", "12")
    char time_str[6];           // Scheduled time as "HH:MM"
    time_t departure_timestamp; // Exact departure timestamp for sorting
    int minutes_until;          // Minutes until departure
    int delay_minutes;          // Delay in minutes (0 if on time)
    bool valid;
    char journey_name[64];      // Unique journey reference (e.g. "S11 19055")
};

// Train details structure (stops/capacity)
struct train_detail_stop {
    char name[64];
    char arrival[16];   // HH:MM
    char departure[16]; // HH:MM
    int delay;          // Minutes
};

struct view_data_train_details {
    char name[64];             // Train name
    char operator[32];         // e.g. SBB
    char capacity_1st[16];     // Low/High/Unknown
    char capacity_2nd[16];     // Low/High/Unknown
    struct train_detail_stop stops[30]; // Max 30 intermediate stops
    int stop_count;
    bool loading;
    bool error;
    char error_msg[64];
};

// Bus details structure (identical to train for now)
struct view_data_bus_details {
    char name[64];
    char operator[32];
    char capacity_1st[16];
    char capacity_2nd[16];
    struct train_detail_stop stops[30];
    int stop_count;
    bool loading;
    bool error;
    char error_msg[64];
};

// Screen A: Bus Countdown
struct view_data_bus_countdown {
    char stop_name[64];        // Bus stop name
    struct bus_departure_view departures[MAX_DEPARTURES];
    int count;
    
    // Dynamic directions
    char directions[MAX_DIRECTIONS][64]; // Names of directions (e.g. "Next stop: Bahnhof")
    int direction_count;                 // Number of valid directions found
    
    time_t update_time;
    bool api_error;
    char error_msg[64];
};

// Screen B: Train Station Board
struct view_data_train_station {
    char station_name[64];     // Train station name
    struct train_departure_view departures[MAX_DEPARTURES];
    int count;
    time_t update_time;
    bool api_error;
    char error_msg[64];
};

// Settings screen data
struct view_data_settings {
    struct view_data_wifi_st wifi_status;
    char ip_address[16];
    bool api_status;
    int brightness;
    int sleep_timeout_min;
};

// Refresh configuration
struct view_data_refresh_config {
    int day_refresh_minutes;      // Refresh interval during day (default: 5)
    int night_refresh_minutes;    // Refresh interval during night (default: 120)
    int day_start_hour;           // Day mode start hour (default: 6)
    int day_end_hour;              // Day mode end hour (default: 21)
};

// View events
enum {
    VIEW_EVENT_SCREEN_START = 0,
    
    VIEW_EVENT_TIME,
    
    VIEW_EVENT_WIFI_ST,
    VIEW_EVENT_WIFI_LIST,
    VIEW_EVENT_WIFI_LIST_REQ,
    VIEW_EVENT_WIFI_CONNECT,
    VIEW_EVENT_WIFI_CONNECT_RET,
    VIEW_EVENT_WIFI_CFG_DELETE,
    VIEW_EVENT_WIFI_SET_BACKUP,   /* struct view_data_wifi_config * – save as backup network */
    
    VIEW_EVENT_TIME_CFG_UPDATE,
    VIEW_EVENT_TIME_CFG_APPLY,
    
    VIEW_EVENT_DISPLAY_CFG,
    VIEW_EVENT_BRIGHTNESS_UPDATE,
    VIEW_EVENT_DISPLAY_CFG_APPLY,
    
    VIEW_EVENT_BUS_COUNTDOWN_UPDATE,    // struct view_data_bus_countdown
    VIEW_EVENT_TRAIN_STATION_UPDATE,    // struct view_data_train_station
    VIEW_EVENT_TRAIN_DETAILS_UPDATE,    // struct view_data_train_details
    VIEW_EVENT_TRANSPORT_REFRESH,       // NULL - trigger manual refresh all
    VIEW_EVENT_BUS_REFRESH,             // NULL - trigger manual refresh bus
    VIEW_EVENT_TRAIN_REFRESH,           // NULL - trigger manual refresh train
    VIEW_EVENT_TRAIN_DETAILS_REQ,       // char* journey_name - request details
    VIEW_EVENT_BUS_DETAILS_UPDATE,      // struct view_data_bus_details
    VIEW_EVENT_BUS_DETAILS_REQ,         // char* journey_name - request details
    VIEW_EVENT_SETTINGS_UPDATE,         // struct view_data_settings
    
    VIEW_EVENT_SHUTDOWN,
    VIEW_EVENT_FACTORY_RESET,
    VIEW_EVENT_SCREEN_CTRL,
    
    VIEW_EVENT_ALL,
};

#ifdef __cplusplus
}
#endif

#endif // VIEW_DATA_H
