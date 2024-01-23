#include "driver/twai.h"
//#include "taskCPP.h"
#include <Arduino.h>

#define HW_NAME "bb_bms"

// Firmware version
#define FW_VERSION_MAJOR			6
#define FW_VERSION_MINOR			00
// Set to 0 for building a release and iterate during beta test builds
#define FW_TEST_VERSION_NUMBER		0

// Settings
#define PACKET_MAX_PL_LEN 512
#define RX_FRAMES_SIZE 100
#define RX_BUFFER_SIZE PACKET_MAX_PL_LEN
#define CAN_STATUS_MSGS_TO_STORE 10
#define CAN_BMS_STATUS_MSGS_TO_STORE 185
#define HW_ADC_TEMP_SENSORS 6

#define UTILS_AGE_S(x) (millis() - x)

static const char *ESP32_CAN_STATUS_STRINGS[] = {
	"STOPPED",				 // CAN_STATE_STOPPED
	"RUNNING",				 // CAN_STATE_RUNNING
	"OFF / RECOVERY NEEDED", // CAN_STATE_BUS_OFF
	"RECOVERY UNDERWAY"		 // CAN_STATE_RECOVERING
};

typedef enum
{
	HW_TYPE_VESC = 0,
	HW_TYPE_VESC_BMS,
	HW_TYPE_CUSTOM_MODULE
} HW_TYPE;

typedef struct
{
	int id;
	uint32_t rx_time;
	float rpm;
	float current;
	float duty;
} can_status_msg;

typedef struct
{
	int id;
	uint32_t rx_time;
	float amp_hours;
	float amp_hours_charged;
} can_status_msg_2;

typedef struct
{
	int id;
	uint32_t rx_time;
	float watt_hours;
	float watt_hours_charged;
} can_status_msg_3;

typedef struct
{
	int id;
	uint32_t rx_time;
	float temp_fet;
	float temp_motor;
	float current_in;
	float pid_pos_now;
} can_status_msg_4;

typedef struct
{
	int id;
	uint32_t rx_time;
	float v_in;
	uint32_t tacho_value;
} can_status_msg_5;

typedef struct
{
	int id;
	uint32_t rx_time;
	float v_cell_min;
	float v_cell_max;
	float t_cell_max;
	float soc;
	float soh;
	bool is_charging;
	bool is_balancing;
	bool is_charge_allowed;
} bms_soc_soh_temp_stat;

typedef enum
{
	FAULT_CODE_NONE = 0,
	FAULT_CODE_CHARGE_OVERCURRENT,
	FAULT_CODE_CHARGE_OVERTEMP
} bms_fault_code;

// Logged fault data
typedef struct
{
	bms_fault_code fault;
	uint32_t fault_time;
	float current;
	float current_ic;
	float temp_batt;
	float temp_pcb;
	float temp_ic;
	float v_cell_min;
	float v_cell_max;
} fault_data;

// CAN commands
typedef enum {
	CAN_PACKET_SET_DUTY = 0,
	CAN_PACKET_SET_CURRENT,
	CAN_PACKET_SET_CURRENT_BRAKE,
	CAN_PACKET_SET_RPM,
	CAN_PACKET_SET_POS,
	CAN_PACKET_FILL_RX_BUFFER,
	CAN_PACKET_FILL_RX_BUFFER_LONG,
	CAN_PACKET_PROCESS_RX_BUFFER,
	CAN_PACKET_PROCESS_SHORT_BUFFER,
	CAN_PACKET_STATUS,
	CAN_PACKET_SET_CURRENT_REL,
	CAN_PACKET_SET_CURRENT_BRAKE_REL,
	CAN_PACKET_SET_CURRENT_HANDBRAKE,
	CAN_PACKET_SET_CURRENT_HANDBRAKE_REL,
	CAN_PACKET_STATUS_2,
	CAN_PACKET_STATUS_3,
	CAN_PACKET_STATUS_4,
	CAN_PACKET_PING,
	CAN_PACKET_PONG,
	CAN_PACKET_DETECT_APPLY_ALL_FOC,
	CAN_PACKET_DETECT_APPLY_ALL_FOC_RES,
	CAN_PACKET_CONF_CURRENT_LIMITS,
	CAN_PACKET_CONF_STORE_CURRENT_LIMITS,
	CAN_PACKET_CONF_CURRENT_LIMITS_IN,
	CAN_PACKET_CONF_STORE_CURRENT_LIMITS_IN,
	CAN_PACKET_CONF_FOC_ERPMS,
	CAN_PACKET_CONF_STORE_FOC_ERPMS,
	CAN_PACKET_STATUS_5,
	CAN_PACKET_POLL_TS5700N8501_STATUS,
	CAN_PACKET_CONF_BATTERY_CUT,
	CAN_PACKET_CONF_STORE_BATTERY_CUT,
	CAN_PACKET_SHUTDOWN,
	CAN_PACKET_IO_BOARD_ADC_1_TO_4,
	CAN_PACKET_IO_BOARD_ADC_5_TO_8,
	CAN_PACKET_IO_BOARD_ADC_9_TO_12,
	CAN_PACKET_IO_BOARD_DIGITAL_IN,
	CAN_PACKET_IO_BOARD_SET_OUTPUT_DIGITAL,
	CAN_PACKET_IO_BOARD_SET_OUTPUT_PWM,
	CAN_PACKET_BMS_V_TOT,
	CAN_PACKET_BMS_I,
	CAN_PACKET_BMS_AH_WH,
	CAN_PACKET_BMS_V_CELL,
	CAN_PACKET_BMS_BAL,
	CAN_PACKET_BMS_TEMPS,
	CAN_PACKET_BMS_HUM,
	CAN_PACKET_BMS_SOC_SOH_TEMP_STAT,
	CAN_PACKET_PSW_STAT,
	CAN_PACKET_PSW_SWITCH,
	CAN_PACKET_BMS_HW_DATA_1,
	CAN_PACKET_BMS_HW_DATA_2,
	CAN_PACKET_BMS_HW_DATA_3,
	CAN_PACKET_BMS_HW_DATA_4,
	CAN_PACKET_BMS_HW_DATA_5,
	CAN_PACKET_BMS_AH_WH_CHG_TOTAL,
	CAN_PACKET_BMS_AH_WH_DIS_TOTAL,
	CAN_PACKET_UPDATE_PID_POS_OFFSET,
	CAN_PACKET_POLL_ROTOR_POS,
	CAN_PACKET_NOTIFY_BOOT,
	CAN_PACKET_MAKE_ENUM_32_BITS = 0xFFFFFFFF,
} CAN_PACKET_ID;

// Communication commands
typedef enum {
	COMM_FW_VERSION = 0,
	COMM_JUMP_TO_BOOTLOADER,
	COMM_ERASE_NEW_APP,
	COMM_WRITE_NEW_APP_DATA,
	COMM_GET_VALUES,
	COMM_SET_DUTY,
	COMM_SET_CURRENT,
	COMM_SET_CURRENT_BRAKE,
	COMM_SET_RPM,
	COMM_SET_POS,
	COMM_SET_HANDBRAKE,
	COMM_SET_DETECT,
	COMM_SET_SERVO_POS,
	COMM_SET_MCCONF,
	COMM_GET_MCCONF,
	COMM_GET_MCCONF_DEFAULT,
	COMM_SET_APPCONF,
	COMM_GET_APPCONF,
	COMM_GET_APPCONF_DEFAULT,
	COMM_SAMPLE_PRINT,
	COMM_TERMINAL_CMD,
	COMM_PRINT,
	COMM_ROTOR_POSITION,
	COMM_EXPERIMENT_SAMPLE,
	COMM_DETECT_MOTOR_PARAM,
	COMM_DETECT_MOTOR_R_L,
	COMM_DETECT_MOTOR_FLUX_LINKAGE,
	COMM_DETECT_ENCODER,
	COMM_DETECT_HALL_FOC,
	COMM_REBOOT,
	COMM_ALIVE,
	COMM_GET_DECODED_PPM,
	COMM_GET_DECODED_ADC,
	COMM_GET_DECODED_CHUK,
	COMM_FORWARD_CAN,
	COMM_SET_CHUCK_DATA,
	COMM_CUSTOM_APP_DATA,
	COMM_NRF_START_PAIRING,
	COMM_GPD_SET_FSW,
	COMM_GPD_BUFFER_NOTIFY,
	COMM_GPD_BUFFER_SIZE_LEFT,
	COMM_GPD_FILL_BUFFER,
	COMM_GPD_OUTPUT_SAMPLE,
	COMM_GPD_SET_MODE,
	COMM_GPD_FILL_BUFFER_INT8,
	COMM_GPD_FILL_BUFFER_INT16,
	COMM_GPD_SET_BUFFER_INT_SCALE,
	COMM_GET_VALUES_SETUP,
	COMM_SET_MCCONF_TEMP,
	COMM_SET_MCCONF_TEMP_SETUP,
	COMM_GET_VALUES_SELECTIVE,
	COMM_GET_VALUES_SETUP_SELECTIVE,
	COMM_EXT_NRF_PRESENT,
	COMM_EXT_NRF_ESB_SET_CH_ADDR,
	COMM_EXT_NRF_ESB_SEND_DATA,
	COMM_EXT_NRF_ESB_RX_DATA,
	COMM_EXT_NRF_SET_ENABLED,
	COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP,
	COMM_DETECT_APPLY_ALL_FOC,
	COMM_JUMP_TO_BOOTLOADER_ALL_CAN,
	COMM_ERASE_NEW_APP_ALL_CAN,
	COMM_WRITE_NEW_APP_DATA_ALL_CAN,
	COMM_PING_CAN,
	COMM_APP_DISABLE_OUTPUT,
	COMM_TERMINAL_CMD_SYNC,
	COMM_GET_IMU_DATA,
	COMM_BM_CONNECT,
	COMM_BM_ERASE_FLASH_ALL,
	COMM_BM_WRITE_FLASH,
	COMM_BM_REBOOT,
	COMM_BM_DISCONNECT,
	COMM_BM_MAP_PINS_DEFAULT,
	COMM_BM_MAP_PINS_NRF5X,
	COMM_ERASE_BOOTLOADER,
	COMM_ERASE_BOOTLOADER_ALL_CAN,
	COMM_PLOT_INIT,
	COMM_PLOT_DATA,
	COMM_PLOT_ADD_GRAPH,
	COMM_PLOT_SET_GRAPH,
	COMM_GET_DECODED_BALANCE,
	COMM_BM_MEM_READ,
	COMM_WRITE_NEW_APP_DATA_LZO,
	COMM_WRITE_NEW_APP_DATA_ALL_CAN_LZO,
	COMM_BM_WRITE_FLASH_LZO,
	COMM_SET_CURRENT_REL,
	COMM_CAN_FWD_FRAME,
	COMM_SET_BATTERY_CUT,
	COMM_SET_BLE_NAME,
	COMM_SET_BLE_PIN,
	COMM_SET_CAN_MODE,
	COMM_GET_IMU_CALIBRATION,
	COMM_GET_MCCONF_TEMP,

	// Custom configuration for hardware
	COMM_GET_CUSTOM_CONFIG_XML,
	COMM_GET_CUSTOM_CONFIG,
	COMM_GET_CUSTOM_CONFIG_DEFAULT,
	COMM_SET_CUSTOM_CONFIG,

	// BMS commands
	COMM_BMS_GET_VALUES,
	COMM_BMS_SET_CHARGE_ALLOWED,
	COMM_BMS_SET_BALANCE_OVERRIDE,
	COMM_BMS_RESET_COUNTERS,
	COMM_BMS_FORCE_BALANCE,
	COMM_BMS_ZERO_CURRENT_OFFSET,

	// FW updates commands for different HW types
	COMM_JUMP_TO_BOOTLOADER_HW,
	COMM_ERASE_NEW_APP_HW,
	COMM_WRITE_NEW_APP_DATA_HW,
	COMM_ERASE_BOOTLOADER_HW,
	COMM_JUMP_TO_BOOTLOADER_ALL_CAN_HW,
	COMM_ERASE_NEW_APP_ALL_CAN_HW,
	COMM_WRITE_NEW_APP_DATA_ALL_CAN_HW,
	COMM_ERASE_BOOTLOADER_ALL_CAN_HW,

	COMM_SET_ODOMETER,

	// Power switch commands
	COMM_PSW_GET_STATUS,
	COMM_PSW_SWITCH,

	COMM_BMS_FWD_CAN_RX,
	COMM_BMS_HW_DATA,
	COMM_GET_BATTERY_CUT,
	COMM_BM_HALT_REQ,
	COMM_GET_QML_UI_HW,
	COMM_GET_QML_UI_APP,
	COMM_CUSTOM_HW_DATA,
	COMM_QMLUI_ERASE,
	COMM_QMLUI_WRITE,

	// IO Board
	COMM_IO_BOARD_GET_ALL,
	COMM_IO_BOARD_SET_PWM,
	COMM_IO_BOARD_SET_DIGITAL,

	COMM_BM_MEM_WRITE,
	COMM_BMS_BLNC_SELFTEST,
	COMM_GET_EXT_HUM_TMP,
	COMM_GET_STATS,
	COMM_RESET_STATS,

	// Lisp
	COMM_LISP_READ_CODE,
	COMM_LISP_WRITE_CODE,
	COMM_LISP_ERASE_CODE,
	COMM_LISP_SET_RUNNING,
	COMM_LISP_GET_STATS,
	COMM_LISP_PRINT,

	COMM_BMS_SET_BATT_TYPE,
	COMM_BMS_GET_BATT_TYPE,
} COMM_PACKET_ID;

class bms_can
{
public:
	bms_can();
	static void begin(void);
	static int rxcnt(void);
	static int txcnt(void);

private:
	static void initCAN();
	static void can_read_task_static(void *param);
	static void can_read_task();
	static void can_process_task_static(void *param);
	static void can_process_task();
	static void can_status_task_static(void *param);
	static void can_status_task();
	static void can_transmit_eid(uint32_t id, const uint8_t *data, int len);
	static void decode_msg(uint32_t eid, uint8_t *data8, int len, bool is_replaced);
	static bool can_ping(uint8_t controller_id, HW_TYPE *hw_type);
	static void sleep_reset();
	static void can_send_buffer(uint8_t controller_id, uint8_t *data, unsigned int len, uint8_t send);
	static void commands_process_packet(unsigned char *data, unsigned int len);
    static void send_packet_wrapper(unsigned char *data, unsigned int len);
	// Private variables
	static TaskHandle_t can_read_task_handle;
	static TaskHandle_t can_process_task_handle;
	static TaskHandle_t can_status_task_handle;
	// Task can_task;
	static QueueHandle_t queue_canrx;
	static QueueHandle_t queue_ping;
	static can_status_msg stat_msgs[CAN_STATUS_MSGS_TO_STORE];
	static can_status_msg_2 stat_msgs_2[CAN_STATUS_MSGS_TO_STORE];
	static can_status_msg_3 stat_msgs_3[CAN_STATUS_MSGS_TO_STORE];
	static can_status_msg_4 stat_msgs_4[CAN_STATUS_MSGS_TO_STORE];
	static can_status_msg_5 stat_msgs_5[CAN_STATUS_MSGS_TO_STORE];
	static bms_soc_soh_temp_stat bms_stat_msgs[CAN_BMS_STATUS_MSGS_TO_STORE];
	static bms_soc_soh_temp_stat bms_stat_v_cell_min;
	static twai_message_t rx_frames[RX_FRAMES_SIZE];
	static int rx_count;
	static int tx_count;
	static volatile HW_TYPE ping_hw_last;
	static uint8_t rx_buffer[RX_BUFFER_SIZE];
	static unsigned int rx_buffer_last_id;

	// TODO: move these to a BMS accessor
	static float bms_if_get_v_tot();
	static float bms_if_get_v_charge();
	static float bms_if_get_i_in();
	static float bms_if_get_i_in_ic();
	static float bms_if_get_ah_cnt();
	static float bms_if_get_wh_cnt();
	static float bms_if_get_v_cell(int cell);
	static float bms_if_get_soc();
	static float bms_if_get_soh();
	static bool bms_if_is_charging();
	static bool bms_if_is_charge_allowed();
	static bool bms_if_is_balancing();
	static bool bms_if_is_balancing_cell(int cell);
	static float bms_if_get_v_cell_min();
	static float bms_if_get_v_cell_max();
	static float bms_if_get_humidity_sensor_temp();
	static float bms_if_get_humitidy();
	static float bms_if_get_temp_ic();
	static int HW_TEMP_CELLS_MAX();
	static float bms_if_get_temp(int sensor);
};
