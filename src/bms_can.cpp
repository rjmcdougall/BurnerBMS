// #include <TelnetStream.h>
#include "bms_can.h"
#include "crc16.h"
#include "buffer.h"
#include "bms.h"

#define USE_ESP_IDF_LOG 1
static constexpr const char *TAG = "bms_can";

#define CAN_DEBUG 1

#define CAN_ADDR 126
#define CAN_ADDR_DIEBIE 10

// TODO: Fix the send buffer allocations

bms_can::bms_can()
{
}

void bms_can::begin(void)
{
	if (CAN_DEBUG)
	{
		BLog_d(TAG, "Initializing bms_can...");
	}
	initCAN();
}

TaskHandle_t bms_can::can_read_task_handle = nullptr;
TaskHandle_t bms_can::can_process_task_handle = nullptr;
TaskHandle_t bms_can::can_status_task_handle = nullptr;
QueueHandle_t bms_can::queue_canrx = nullptr;
QueueHandle_t bms_can::queue_ping = nullptr;

can_status_msg bms_can::stat_msgs[CAN_STATUS_MSGS_TO_STORE];
can_status_msg_2 bms_can::stat_msgs_2[CAN_STATUS_MSGS_TO_STORE];
can_status_msg_3 bms_can::stat_msgs_3[CAN_STATUS_MSGS_TO_STORE];
can_status_msg_4 bms_can::stat_msgs_4[CAN_STATUS_MSGS_TO_STORE];
can_status_msg_5 bms_can::stat_msgs_5[CAN_STATUS_MSGS_TO_STORE];
bms_soc_soh_temp_stat bms_can::bms_stat_msgs[CAN_BMS_STATUS_MSGS_TO_STORE];
bms_soc_soh_temp_stat bms_can::bms_stat_v_cell_min;

unsigned int bms_can::rx_buffer_last_id = -1;
uint8_t bms_can::rx_buffer[RX_BUFFER_SIZE];

int bms_can::rx_count = 0;
int bms_can::tx_count = 0;

volatile HW_TYPE bms_can::ping_hw_last = HW_TYPE_VESC;

static void send_packet_wrapper(unsigned char *data, unsigned int len);

void bms_can::can_read_task_static(void *param)
{
	static_cast<bms_can *>(param)->can_read_task();
}

void bms_can::can_process_task_static(void *param)
{
	static_cast<bms_can *>(param)->can_process_task();
}

void bms_can::can_status_task_static(void *param)
{
	static_cast<bms_can *>(param)->can_status_task();
}

void enable_alerts()
{
	// Reconfigure alerts to detect Error Passive and Bus-Off error states

	uint32_t alerts_to_enable = TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF;
	if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK)
	{
		BLog_i(TAG, "Alerts reconfigured\n");
	}
	else
	{
		BLog_i(TAG, "Failed to reconfigure alerts");
	}
}

void bms_can::initCAN()
{
	for (int i = 0; i < CAN_STATUS_MSGS_TO_STORE; i++)
	{
		stat_msgs[i].id = -1;
		stat_msgs_2[i].id = -1;
		stat_msgs_3[i].id = -1;
		stat_msgs_4[i].id = -1;
		stat_msgs_5[i].id = -1;
	}

	bms_stat_v_cell_min.id = -1;

	for (int i = 0; i < CAN_BMS_STATUS_MSGS_TO_STORE; i++)
	{
		bms_stat_msgs[i].id = -1;
	}

	// xTaskCreate(bms_can::can_read_task_static, "bms_can", 3000, nullptr, 1, &bms_can::can_task_handle);
	// Task can_task = new Task("can_task", can_read_task, 1, 16);
	//  Initialize configuration structures using macro initializers
	// can_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(gpio_num_t::GPIO_NUM_5, gpio_num_t::GPIO_NUM_4, TWAI_MODE_LISTEN_ONLY);
	// can_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(gpio_num_t::GPIO_NUM_5, gpio_num_t::GPIO_NUM_35, TWAI_MODE_NORMAL);
	// can_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(gpio_num_t::GPIO_NUM_13, gpio_num_t::GPIO_NUM_35, TWAI_MODE_NORMAL);
	// can_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(gpio_num_t::GPIO_NUM_13, gpio_num_t::GPIO_NUM_36, TWAI_MODE_NORMAL);
	twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(gpio_num_t::GPIO_NUM_26, gpio_num_t::GPIO_NUM_36, TWAI_MODE_NORMAL);
	// g_config.mode = TWAI_MODE_NORMAL;
	twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
	// twai_timing_config_t t_config = TWAI_TIMING_CONFIG_50KBITS();
	twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
	// pinMode(GPIO_NUM_32, INPUT_PULLDOWN);

	// Install CAN driver
	if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK)
	{
		BLog_i(TAG, "CAN Driver installed\n");
	}
	else
	{
		BLog_i(TAG, "Failed to install CAN driver\n");
	}

	// Let CANBUS start
	// vTaskDelay(pdMS_TO_TICKS(3000));

	enable_alerts();

	// Start CAN driver
	if (twai_start() == ESP_OK)
	{
		BLog_i(TAG, "CAN Driver started\n");
	}
	else
	{
		BLog_i(TAG, "Failed to start driver\n");
	}

	// TODO: move these to init only once
	BLog_i(TAG, "CAN  init task queue_canrx = %lx\n", &bms_can::queue_canrx);
	xTaskCreate(bms_can::can_read_task_static, "bms_can_read", 16384, nullptr, 1, nullptr);
	xTaskCreate(bms_can::can_process_task_static, "bms_can_process", 16384, nullptr, 1, nullptr);
	vTaskDelay(pdMS_TO_TICKS(5000));
	xTaskCreate(bms_can::can_status_task_static, "bms_can_status", 16384, nullptr, 1, nullptr);
}

// TODO: For power management
void bms_can::sleep_reset()
{
}

// Switch CANBUS off, saves a couple of milliamps
// hal.CANBUSEnable(false);

void bms_can::can_read_task()
{
	vTaskDelay(pdMS_TO_TICKS(2000));
	BLog_i(TAG, "CAN  starting read task\n");
	bms_can::queue_canrx = xQueueCreate(10, sizeof(twai_message_t));
	BLog_i(TAG, "CAN  read task queue_canrx = %lx\n", &bms_can::queue_canrx);
	if (bms_can::queue_canrx == nullptr)
	{
		BLog_i(TAG, "CAN  null queue\n");
		return;
	}
	twai_message_t message;
	while (1)
	{

		// Wait for message to be received
		esp_err_t res = twai_receive(&message, pdMS_TO_TICKS(8000));

		if (res == ESP_OK)
		{
			// BLog_i(TAG, "CAN Message received id = %d\n", message.identifier);
			if (!(message.flags & TWAI_MSG_FLAG_RTR))
			{
				xQueueSendToBack(bms_can::queue_canrx, &message, 16);
				rx_count++;
			}
		}
		else if (res == ESP_ERR_TIMEOUT)
		{
			/// ignore the timeout or do something
			// BLog_i(TAG, "Timeout");
		}
	}
}

void bms_can::can_process_task()
{
	vTaskDelay(pdMS_TO_TICKS(2000));
	BLog_i(TAG, "CAN  starting process task\n");
	BLog_i(TAG, "CAN  process task queue_canrx = %lx\n", &bms_can::queue_canrx);
	BLog_i(TAG, "CAN  process task queue_canrx contents = %lx\n", bms_can::queue_canrx);
	twai_message_t rxmsg;
	for (;;)
	{
		if (xQueueReceive(bms_can::queue_canrx, &rxmsg, portMAX_DELAY) == pdPASS)
		{
			// BLog_i(TAG, "CAN  process got msg %x %x %d!\n", rxmsg.identifier, rxmsg.flags, rxmsg.data_length_code);
			if (rxmsg.flags == TWAI_MSG_FLAG_EXTD)
			{
				decode_msg(rxmsg.identifier, &rxmsg.data[0], rxmsg.data_length_code, false);
			}
			else
			{
				// if (sid_callback) {
				//	sid_callback(rxmsg.SID, rxmsg.data8, rxmsg.data_length_code);
				// }
			}
		}
	}
}

void bms_can::commands_send_packet(unsigned char *data, unsigned int len)
{
	//BLog_i(TAG, "CAN  send packet: \n");
	can_send_buffer(rx_buffer_last_id, data, len, 1);
}

void bms_can::can_send_buffer(uint8_t controller_id, uint8_t *data, unsigned int len, uint8_t send)
{
	uint8_t send_buffer[8];
	//BLog_i(TAG, "CAN  send buffer: \n");

	if (len <= 6)
	{
		// BLog_i(TAG, "CAN  send buffer short: \n");
		uint32_t ind = 0;
		send_buffer[ind++] = CAN_ADDR;
		send_buffer[ind++] = send;
		memcpy(send_buffer + ind, data, len);
		ind += len;
		can_transmit_eid(controller_id |
							 ((uint32_t)CAN_PACKET_PROCESS_SHORT_BUFFER << 8),
						 send_buffer, ind);
	}
	else
	{
		// BLog_i(TAG, "CAN  send buffer long: \n");
		unsigned int end_a = 0;
		for (unsigned int i = 0; i < len; i += 7)
		{
			if (i > 255)
			{
				break;
			}

			end_a = i + 7;

			uint8_t send_len = 7;
			send_buffer[0] = i;

			if ((i + 7) <= len)
			{
				memcpy(send_buffer + 1, data + i, send_len);
			}
			else
			{
				send_len = len - i;
				memcpy(send_buffer + 1, data + i, send_len);
			}

			can_transmit_eid(controller_id |
								 ((uint32_t)CAN_PACKET_FILL_RX_BUFFER << 8),
							 send_buffer, send_len + 1);
		}

		for (unsigned int i = end_a; i < len; i += 6)
		{
			uint8_t send_len = 6;
			send_buffer[0] = i >> 8;
			send_buffer[1] = i & 0xFF;

			if ((i + 6) <= len)
			{
				memcpy(send_buffer + 2, data + i, send_len);
			}
			else
			{
				send_len = len - i;
				memcpy(send_buffer + 2, data + i, send_len);
			}

			can_transmit_eid(controller_id |
								 ((uint32_t)CAN_PACKET_FILL_RX_BUFFER_LONG << 8),
							 send_buffer, send_len + 2);
		}

		uint32_t ind = 0;
		send_buffer[ind++] = CAN_ADDR;
		send_buffer[ind++] = send;
		send_buffer[ind++] = len >> 8;
		send_buffer[ind++] = len & 0xFF;
		unsigned short crc = CRC16::CalculateArray(data, len);
		send_buffer[ind++] = (uint8_t)(crc >> 8);
		send_buffer[ind++] = (uint8_t)(crc & 0xFF);

		can_transmit_eid(controller_id |
							 ((uint32_t)CAN_PACKET_PROCESS_RX_BUFFER << 8),
						 send_buffer, ind++);
	}
}

// void bms_can::commands_process_packet(unsigned char *data, unsigned int len, void(*reply_func)(unsigned char *data, unsigned int len)
void bms_can::commands_process_packet(unsigned char *data, unsigned int len)
{
	//	if (len < 1 || reply_func == 0) {
	if (len < 1)
	{
		return;
	}

	// send_func = reply_func;

	unsigned int packet_id = data[0] & 0xFF;
	data++;
	len--;

	char buffer[128];
	// BLog_i(TAG, "CAN  process command packet: %d\n", packet_id);

	// sprintf(buffer, "CAN  command %x\n", packet_id);

	//	TelnetStream.println(buffer);

	switch (packet_id)
	{
	case COMM_FW_VERSION:
	{
		// BLog_i(TAG, "CAN  process packet: COMM_FW_VERSION\n");

		int32_t ind = 0;
		uint8_t send_buffer[50];
		send_buffer[ind++] = COMM_FW_VERSION;
		send_buffer[ind++] = FW_VERSION_MAJOR;
		send_buffer[ind++] = FW_VERSION_MINOR;

		strcpy((char *)(send_buffer + ind), HW_NAME);
		ind += strlen(HW_NAME) + 1;

		uint64_t esp_chip_id[2];
		esp_chip_id[0] = ESP.getEfuseMac();

		memcpy(send_buffer + ind, &esp_chip_id[0], 12);
		ind += 12;

		send_buffer[ind++] = 0;
		send_buffer[ind++] = FW_TEST_VERSION_NUMBER;

		send_buffer[ind++] = HW_TYPE_VESC_BMS;

		send_buffer[ind++] = 1; // No custom config

		// reply_func(send_buffer, ind);
		can_send_buffer(rx_buffer_last_id, send_buffer, ind, 1);
		BLog_d(TAG, "Sent COMM_FW_VERSION to %d", rx_buffer_last_id);
	}
	break;

	case COMM_FW_VERSION_DIEBIE:
	{
		// BLog_i(TAG, "CAN  process packet: COMM_FW_VERSIONDIEBIE\n");

		int32_t ind = 0;
		uint8_t send_buffer[50];
		send_buffer[ind++] = COMM_FW_VERSION;
		send_buffer[ind++] = DIEBIE_VERSION_MAJOR;
		send_buffer[ind++] = DIEBIE_VERSION_MINOR;

		strcpy((char *)(send_buffer + ind), DIEBIE_HW_NAME);
		ind += strlen(HW_NAME) + 1;

		uint64_t esp_chip_id[2];
		esp_chip_id[0] = ESP.getEfuseMac();

		memcpy(send_buffer + ind, &esp_chip_id[0], 12);
		ind += 12;

		// reply_func(send_buffer, ind);
		can_send_buffer(rx_buffer_last_id, send_buffer, ind, 1);
		BLog_d(TAG, "Sent COMM_FW_VERSION_DIEBIE to %d", rx_buffer_last_id);
	}
	break;

	case COMM_BMS_GET_VALUES:
	{
		int32_t ind = 0;

		uint8_t send_buffer_global[256];

		send_buffer_global[ind++] = packet_id;

		buffer_append_float32(send_buffer_global, bms_if_get_v_tot(), 1e6, &ind);
		buffer_append_float32(send_buffer_global, bms_if_get_v_charge(), 1e6, &ind);
		buffer_append_float32(send_buffer_global, bms_if_get_i_in(), 1e6, &ind);
		buffer_append_float32(send_buffer_global, bms_if_get_i_in_ic(), 1e6, &ind);
		buffer_append_float32(send_buffer_global, bms_if_get_ah_cnt(), 1e3, &ind);
		buffer_append_float32(send_buffer_global, bms_if_get_wh_cnt(), 1e3, &ind);

		// Cell voltages
		send_buffer_global[ind++] = bms.getNumberCells();
		for (int i = 0; i <
						(bms.getNumberCells());
			 i++)
		{
			buffer_append_float16(send_buffer_global, bms_if_get_v_cell(i), 1e3, &ind);
		}

		// Balancing state
		for (int i = 0; i <
						(bms.getNumberCells());
			 i++)
		{
			send_buffer_global[ind++] = bms_if_is_balancing_cell(i);
		}

#define HW_TEMP_SENSORS 2

		// Temperatures
		send_buffer_global[ind++] = HW_TEMP_SENSORS;
		for (int i = 0; i < HW_TEMP_SENSORS; i++)
		{
			buffer_append_float16(send_buffer_global, bms_if_get_temp(i), 1e2, &ind);
		}
		buffer_append_float16(send_buffer_global, bms_if_get_temp_ic(), 1e2, &ind);

		// Humidity
		buffer_append_float16(send_buffer_global, 0, 1e2, &ind);
		buffer_append_float16(send_buffer_global, 0, 1e2, &ind);

		// Highest cell temperature
		buffer_append_float16(send_buffer_global, HW_TEMP_CELLS_MAX(), 1e2, &ind);

		// State of charge and state of health
		buffer_append_float16(send_buffer_global, bms_if_get_soc(), 1e3, &ind);
		buffer_append_float16(send_buffer_global, bms_if_get_soh(), 1e3, &ind);

		// CAN ID
		send_buffer_global[ind++] = CAN_ADDR;

		// Total charge and discharge counters
		buffer_append_float32_auto(send_buffer_global, 0, &ind);
		buffer_append_float32_auto(send_buffer_global, 0, &ind);
		buffer_append_float32_auto(send_buffer_global, 0, &ind);
		buffer_append_float32_auto(send_buffer_global, 0, &ind);
		//	buffer_append_float32_auto(send_buffer_global, bms_if_get_ah_cnt_chg_total(), &ind);
		//		buffer_append_float32_auto(send_buffer_global, bms_if_get_wh_cnt_chg_total(), &ind);
		//		buffer_append_float32_auto(send_buffer_global, bms_if_get_ah_cnt_dis_total(), &ind);
		//		buffer_append_float32_auto(send_buffer_global, bms_if_get_wh_cnt_dis_total(), &ind);
		can_send_buffer(rx_buffer_last_id, send_buffer_global, ind, 1);
		BLog_d(TAG, "Sent COMM_BMS_GET_VALUES");
	}
	break;

	case COMM_BMS_GET_BATT_TYPE:
	{
		int32_t ind;
		uint8_t send_buffer_global[256];

		ind = 0;
		int battery_type = 1; // TODO
		send_buffer_global[ind++] = packet_id;
		buffer_append_uint32(send_buffer_global, battery_type, &ind);
		commands_send_packet(send_buffer_global, ind);
		BLog_d(TAG, "Sent COMM_BMS_GET_BATT_TYPE");
	}
	break;

	// Emulate DiEBie to make metr.at happy
	case COM_GET_BMS_CELLS_DIEBIE:
	{
		int32_t ind;
		ind = 0;
		uint8_t send_buffer_global[256];
		send_buffer_global[ind++] = 51; // original COM_GET_BMS_CELLS = 51

		send_buffer_global[ind++] = (uint8_t)bms.getNumberCells();

		for (int cell = 0; cell < bms.getNumberCells(); cell++)
		{
			if (bms.isCellBalancing(cell))
			{
				buffer_append_float16(send_buffer_global, bms_if_get_v_cell(cell) * -1.0f, 1e3, &ind);
			}
			else
			{
				buffer_append_float16(send_buffer_global, bms_if_get_v_cell(cell), 1e3, &ind);
			}
		}

		send_buffer_global[ind++] = CAN_ADDR_DIEBIE;
		commands_send_packet(send_buffer_global, ind);
		BLog_d(TAG, "Sent COMM_BMS_GET_CELLS_DIEBIE");
	}
	break;

	case COMM_GET_VALUES_DIEBIE:
	{
		int32_t ind = 0;

		uint8_t send_buffer_global[256];

		send_buffer_global[ind++] = COMM_GET_VALUES;

		buffer_append_float32(send_buffer_global, bms_if_get_v_tot(), 1e3, &ind);
		buffer_append_float32(send_buffer_global, bms_if_get_i_in(), 1e3, &ind);
		
		send_buffer_global[ind++] = (uint8_t)(bms_if_get_soc() * 100);

		buffer_append_float32(send_buffer_global, bms_if_get_v_cell_max(), 1e3, &ind);
		buffer_append_float32(send_buffer_global, bms_if_get_v_cell_avg(), 1e3, &ind);
		buffer_append_float32(send_buffer_global, bms_if_get_v_cell_min(), 1e3, &ind);
		buffer_append_float32(send_buffer_global, bms_if_get_v_cell_max() - bms_if_get_v_cell_min(), 1e3, &ind);

		buffer_append_float16(send_buffer_global, 0, 1e3, &ind);
		buffer_append_float16(send_buffer_global, 0, 1e3, &ind);
		buffer_append_float16(send_buffer_global, 0, 1e3, &ind);
		buffer_append_float16(send_buffer_global, 0, 1e3, &ind);
		buffer_append_float16(send_buffer_global, 0, 1e3, &ind);
		buffer_append_float16(send_buffer_global, 0, 1e3, &ind);

		buffer_append_float16(send_buffer_global, bms_if_get_temp(0), 1e1, &ind);
		buffer_append_float16(send_buffer_global, bms_if_get_temp(1), 1e1, &ind);
		buffer_append_float16(send_buffer_global, bms_if_get_temp_ic(), 1e1, &ind);
		buffer_append_float16(send_buffer_global, bms_if_get_temp_ic(), 1e1, &ind);

		send_buffer_global[ind++] = 0;
		send_buffer_global[ind++] = 0;
		send_buffer_global[ind++] = 0;

		send_buffer_global[ind++] = CAN_ADDR_DIEBIE;
		commands_send_packet(send_buffer_global, ind);
		BLog_d(TAG, "Sent COMM_BMS_GET_VALUES_DIEBIE");
	}
	break;

	default:
		uint32_t data1 = *(data++);
		uint32_t data2 = *(data++);
		uint32_t data3 = *(data++);
		BLog_i(TAG, "CAN  process packet: to id %d Unknown command %d : %d %d %d\n", rx_buffer_last_id, packet_id, data1, data2, data3);

		break;
	}
}

// static void send_packet_wrapper(unsigned char *data, unsigned int len) {
//	can_send_buffer(rx_buffer_last_id, data, len, 1);
// }

void bms_can::decode_msg(uint32_t eid, uint8_t *data8, int len, bool is_replaced)
{
	int32_t ind = 0;
	unsigned int rxbuf_len;
	unsigned int rxbuf_ind;
	uint8_t crc_low;
	uint8_t crc_high;
	uint8_t commands_send;

	uint8_t id = eid & 0xFF;
	int32_t cmd = eid >> 8;
	// BLog_i(TAG, "CAN  (i am addr %0x) decode from id %x cmd %x d0=%x\n", CAN_ADDR, id, cmd, data8[0]);
	// char buffer[128];
	// sprintf(buffer, "CAN  %x decode id %x cmd %x\n", CAN_ADDR, id, cmd);
	// TelnetStream.println(buffer);

	// Emulate DieBie
	// TODO: sends cmd 51, COMM_GET_BMS_CELLS which we remap to COM_GET_BMS_CELLS_DIEBIE
	// TODO: maybe should also check if this is a cmd short buffer
	if (id == CAN_ADDR_DIEBIE)
	{
		unsigned int packet_id = (data8[2] & 0xFF);
		if (packet_id == COMM_FW_VERSION)
		{
			data8[2] = COMM_FW_VERSION_DIEBIE;
		}
		else if (packet_id == 51)
		{
			data8[2] = COM_GET_BMS_CELLS_DIEBIE;
		}
		else if (packet_id == COMM_GET_VALUES)
		{
			data8[2] = COMM_GET_VALUES_DIEBIE;
		}
		//BLog_i(TAG, "CAN  Diebie packet: %d:  %d %d %d\n", cmd, data8[0], data8[1], data8[2]);
	}

	if (id == 255 || id == CAN_ADDR || id == CAN_ADDR_DIEBIE)
	//	if (id == 255 || id == 99)
	{
		// BLog_i(TAG, "CAN  decode 255 or me: id %x cmd %d len %d\n", id, cmd, len);
		//  sprintf(buffer, "CAN  decode 255 or me id %x cmd %x\n", id, cmd);
		//  TelnetStream.println(buffer);
		switch (cmd)
		{
		case CAN_PACKET_FILL_RX_BUFFER:
			//BLog_i(TAG, "CAN  decode: CAN_PACKET_FILL_RX_BUFFER\n");
			memcpy(rx_buffer + data8[0], data8 + 1, len - 1);
			break;

		case CAN_PACKET_FILL_RX_BUFFER_LONG:
			//BLog_i(TAG, "CAN  decode: CAN_PACKET_FILL_RX_BUFFER_LONG\n");
			rxbuf_ind = (unsigned int)data8[0] << 8;
			rxbuf_ind |= data8[1];
			if (rxbuf_ind < RX_BUFFER_SIZE)
			{
				memcpy(rx_buffer + rxbuf_ind, data8 + 2, len - 2);
			}
			break;

		case CAN_PACKET_PROCESS_RX_BUFFER:
			// BLog_i(TAG, "CAN  decode: CAN_PACKET_PROCESS_RX_BUFFER id %x cmd %d, len %d\n", id, cmd, len);
			// sprintf(buffer, "CAN  CAN_PACKET_PROCESS_RX_BUFFER 		id %x cmd %d\n", id, cmd);
			//			TelnetStream.println(buffer);
			//			TelnetStream.flush();
			ind = 0;
			rx_buffer_last_id = data8[ind++];
			commands_send = data8[ind++];
			rxbuf_len = (unsigned int)data8[ind++] << 8;
			rxbuf_len |= (unsigned int)data8[ind++];

			if (rxbuf_len > RX_BUFFER_SIZE)
			{
				break;
			}

			break;

			crc_high = data8[ind++];
			crc_low = data8[ind++];

			if (CRC16::CalculateArray(rx_buffer, rxbuf_len) == ((unsigned short)crc_high << 8 | (unsigned short)crc_low))
			{

				if (is_replaced)
				{
					if (rx_buffer[0] == COMM_JUMP_TO_BOOTLOADER ||
						rx_buffer[0] == COMM_ERASE_NEW_APP ||
						rx_buffer[0] == COMM_WRITE_NEW_APP_DATA ||
						rx_buffer[0] == COMM_WRITE_NEW_APP_DATA_LZO ||
						rx_buffer[0] == COMM_ERASE_BOOTLOADER)
					{
						break;
					}
				}

				sleep_reset();

				switch (commands_send)
				{
				case 0:
					BLog_i(TAG, "CAN  command CAN_PACKET_PROCESS_BUFFER: 0\n");
					// TelnetStream.println("commands_process_packet");
					// TelnetStream.flush();
					// commands_process_packet(rx_buffer, rxbuf_len, send_packet_wrapper);
					commands_process_packet(rx_buffer, rxbuf_len);
					break;
				case 1:
					BLog_i(TAG, "CAN  command CAN_PACKET_PROCESS_BUFFER: 1\n");
					// TelnetStream.println("commands_send_packet");
					// TelnetStream.flush();
					// commands_send_packet_can_last(data8 + ind, len - ind);
					commands_send_packet(rx_buffer, rxbuf_len);
					break;
				case 2:
					BLog_i(TAG, "CAN  command CAN_PACKET_PROCESS_BUFFER: 2\n");
					// TelnetStream.println("commands_process_packet");
					// TelnetStream.flush();
					// commands_process_packet(rx_buffer, rxbuf_len, 0);
					commands_process_packet(rx_buffer, rxbuf_len);
					break;
				default:
					break;
				}
			}
			break;

		case CAN_PACKET_PROCESS_SHORT_BUFFER:

			// BLog_i(TAG, "CAN  decode: CAN_PACKET_PROCESS_SHORT_BUFFER id %x cmd %x  len %d\n", id, cmd, len);

			ind = 0;
			rx_buffer_last_id = data8[ind++];
			commands_send = data8[ind++];

			// BLog_i(TAG, "CAN  decode: CAN_PACKET_PROCESS_SHORT_BUFFER id %x cmd %x  cmd_send %x len %d\n", id, cmd, commands_send, len);
			//  sprintf(buffer, "CAN  CAN_PACKET_PROCESS_SHORT_BUFFER 		id %x cmd %x cmd_send %x\n", id, cmd, commands_send);
			//  TelnetStream.println(buffer);
			//  TelnetStream.flush();

			if (is_replaced)
			{
				if (data8[ind] == COMM_JUMP_TO_BOOTLOADER ||
					data8[ind] == COMM_ERASE_NEW_APP ||
					data8[ind] == COMM_WRITE_NEW_APP_DATA ||
					data8[ind] == COMM_WRITE_NEW_APP_DATA_LZO ||
					data8[ind] == COMM_ERASE_BOOTLOADER)
				{
					break;
				}
			}

			switch (commands_send)
			{
			case 0:
				// BLog_i(TAG, "CAN  command CAN_PACKET_PROCESS_SHORT_BUFFER: 0a\n");
				//  TelnetStream.println("commands_process_packet");
				//  TelnetStream.flush();
				// commands_process_packet(data8 + ind, len - ind, send_packet_wrapper);
				commands_process_packet(data8 + ind, len - ind);
				break;
			case 1:
				BLog_i(TAG, "CAN  command CAN_PACKET_PROCESS_SHORT_BUFFER: 1a\n");
				// TelnetStream.println("commands_send_packet");
				// TelnetStream.flush();
				// commands_process_packet
				commands_send_packet(data8 + ind, len - ind);
				break;
			case 2:
				// TelnetStream.println("commands_process_packet");
				// TelnetStream.flush();
				// commands_process_packet(data8 + ind, len - ind, 0);
				BLog_i(TAG, "CAN  command CAN_PACKET_PROCESS_SHORT_BUFFER: 2a\n");
				commands_process_packet(data8 + ind, len - ind);
				break;
			default:
				break;
			}
			break;

		case CAN_PACKET_PING:
			// BLog_i(TAG, "CAN  decode: CAN_PACKET_PING %x\n", (uint32_t)data8[0] & 0xFF);
			//  TelnetStream.println("ping");
			//  TelnetStream.flush();

			uint8_t buffer[2];
			buffer[0] = CAN_ADDR;
			buffer[1] = HW_TYPE_VESC_BMS;
			can_transmit_eid((uint32_t)(data8[0] |
										((uint32_t)CAN_PACKET_PONG << 8)),
							 buffer, 2);

			break;

		case CAN_PACKET_PONG:
			BLog_i(TAG, "CAN  decode: CAN_PACKET_PONG\n");
			// data8[0]; // Sender ID
			if (queue_ping)
			{
				if (len >= 2)
				{
					ping_hw_last = (HW_TYPE)data8[1];
				}
				else
				{
					ping_hw_last = HW_TYPE_VESC_BMS;
				}
				// TODO: convert ping_hw_last to a message that is passed
				uint32_t dummy_msg = 0;
				xQueueSendToBack(queue_ping, &dummy_msg, 0);
			}
			break;

		case CAN_PACKET_SHUTDOWN:
		{
			// TODO: Implement when hw has power switch
		}
		break;

		default:
			break;
		}
	}

	switch (cmd)
	{
	case CAN_PACKET_PING:
		// BLog_i(TAG, "CAN  (i am addr %0x) decode from id %x cmd %x d0=%x\n", CAN_ADDR, id, cmd, data8[0]);
		// BLog_i(TAG, "CAN  decode: CAN_PACKET_PING 2\n");
		sleep_reset();
		break;

	case CAN_PACKET_STATUS:
		// BLog_i(TAG, "CAN  decode: CAN_PACKET_STATUS\n");
		sleep_reset();

		for (int i = 0; i < CAN_STATUS_MSGS_TO_STORE; i++)
		{
			// BLog_i(TAG, "CAN  decode: CAN_PACKET_STATUS %d\n", i);
			can_status_msg *stat_tmp = &stat_msgs[i];
			// BLog_i(TAG, "CAN  decode: CAN_PACKET_STATUS %d id = %d\n", i, stat_tmp->id);
			if (stat_tmp->id == id || stat_tmp->id == -1)
			{
				ind = 0;
				stat_tmp->id = id;
				stat_tmp->rx_time = millis();
				stat_tmp->rpm = (float)buffer_get_int32(data8, &ind);
				stat_tmp->current = (float)buffer_get_int16(data8, &ind) / 10.0;
				stat_tmp->duty = (float)buffer_get_int16(data8, &ind) / 1000.0;
#define BAJA_HEADLIGHT_PIN GPIO_NUM_13
				if (stat_tmp->rpm > 0)
				{
					// digitalWrite(BAJA_HEADLIGHT_PIN, LOW);
					// TelnetStream.println("h/l on");
					// TelnetStream.flush();
				}
				else
				{
					// TelnetStream.println("h/l off");
					// TelnetStream.flush();
					// digitalWrite(BAJA_HEADLIGHT_PIN, HIGH);
				}
				break;
			}
		}
		break;

	case CAN_PACKET_STATUS_2:
		// BLog_i(TAG, "CAN  decode: CAN_PACKET_STATUS2\n");
		for (int i = 0; i < CAN_STATUS_MSGS_TO_STORE; i++)
		{
			can_status_msg_2 *stat_tmp_2 = &stat_msgs_2[i];
			if (stat_tmp_2->id == id || stat_tmp_2->id == -1)
			{
				ind = 0;
				stat_tmp_2->id = id;
				stat_tmp_2->rx_time = millis();
				stat_tmp_2->amp_hours = (float)buffer_get_int32(data8, &ind) / 1e4;
				stat_tmp_2->amp_hours_charged = (float)buffer_get_int32(data8, &ind) / 1e4;
				break;
			}
		}
		break;

	case CAN_PACKET_STATUS_3:
		// BLog_i(TAG, "CAN  decode: CAN_PACKET_STATUS3\n");
		for (int i = 0; i < CAN_STATUS_MSGS_TO_STORE; i++)
		{
			can_status_msg_3 *stat_tmp_3 = &stat_msgs_3[i];
			if (stat_tmp_3->id == id || stat_tmp_3->id == -1)
			{
				ind = 0;
				stat_tmp_3->id = id;
				stat_tmp_3->rx_time = millis();
				stat_tmp_3->watt_hours = (float)buffer_get_int32(data8, &ind) / 1e4;
				stat_tmp_3->watt_hours_charged = (float)buffer_get_int32(data8, &ind) / 1e4;
				break;
			}
		}
		break;

	case CAN_PACKET_STATUS_4:
		// BLog_i(TAG, "CAN  decode: CAN_PACKET_STATUS4\n");
		for (int i = 0; i < CAN_STATUS_MSGS_TO_STORE; i++)
		{
			can_status_msg_4 *stat_tmp_4 = &stat_msgs_4[i];
			if (stat_tmp_4->id == id || stat_tmp_4->id == -1)
			{
				ind = 0;
				stat_tmp_4->id = id;
				stat_tmp_4->rx_time = millis();
				stat_tmp_4->temp_fet = (float)buffer_get_int16(data8, &ind) / 10.0;
				stat_tmp_4->temp_motor = (float)buffer_get_int16(data8, &ind) / 10.0;
				stat_tmp_4->current_in = (float)buffer_get_int16(data8, &ind) / 10.0;
				stat_tmp_4->pid_pos_now = (float)buffer_get_int16(data8, &ind) / 50.0;
				break;
			}
		}
		break;

	case CAN_PACKET_STATUS_5:
		// BLog_i(TAG, "CAN  decode: CAN_PACKET_STATUS5\n");
		for (int i = 0; i < CAN_STATUS_MSGS_TO_STORE; i++)
		{
			can_status_msg_5 *stat_tmp_5 = &stat_msgs_5[i];
			if (stat_tmp_5->id == id || stat_tmp_5->id == -1)
			{
				ind = 0;
				stat_tmp_5->id = id;
				stat_tmp_5->rx_time = millis();
				stat_tmp_5->tacho_value = buffer_get_int32(data8, &ind);
				stat_tmp_5->v_in = (float)buffer_get_int16(data8, &ind) / 1e1;
				// BLog_i(TAG, "CAN  decode: v_in = %f\n", stat_tmp_5->v_in);
				break;
			}
		}
		break;

	case CAN_PACKET_BMS_SOC_SOH_TEMP_STAT:
		BLog_i(TAG, "CAN  decode: CAN_PACKET_BMS_SOC_SOH_TEMP_STAT\n");
		{
			int32_t ind = 0;
			bms_soc_soh_temp_stat msg;
			msg.id = id;
			msg.rx_time = millis();
			msg.v_cell_min = buffer_get_float16(data8, 1e3, &ind);
			msg.v_cell_max = buffer_get_float16(data8, 1e3, &ind);
			msg.soc = ((float)((uint8_t)data8[ind++])) / 255.0;
			msg.soh = ((float)((uint8_t)data8[ind++])) / 255.0;
			msg.t_cell_max = (float)((int8_t)data8[ind++]);
			uint8_t stat = data8[ind++];
			msg.is_charging = (stat >> 0) & 1;
			msg.is_balancing = (stat >> 1) & 1;
			msg.is_charge_allowed = (stat >> 2) & 1;

			// Do not go to sleep when some other pack is charging or balancing.
			if (msg.is_charging || msg.is_balancing)
			{
				sleep_reset();
			}

			// Find BMS with highest cell voltage
			if (bms_stat_v_cell_min.id < 0 ||
				UTILS_AGE_S(bms_stat_v_cell_min.rx_time) > 2.0 ||
				bms_stat_v_cell_min.v_cell_min > msg.v_cell_min)
			{
				bms_stat_v_cell_min = msg;
			}
			else if (bms_stat_v_cell_min.id == msg.id)
			{
				bms_stat_v_cell_min = msg;
			}

			for (int i = 0; i < CAN_BMS_STATUS_MSGS_TO_STORE; i++)
			{
				bms_soc_soh_temp_stat *msg_buf = &bms_stat_msgs[i];

				// Reset ID after 10 minutes of silence
				if (msg_buf->id != -1 && UTILS_AGE_S(msg_buf->rx_time) > 60 * 10)
				{
					msg_buf->id = -1;
				}

				if (msg_buf->id == id || msg_buf->id == -1)
				{
					*msg_buf = msg;
					break;
				}
			}
		}
		break;

	default:
		// BLog_i(TAG, "CAN  decode: default %d\n", cmd);
		break;
	}
}

/**
 * Check if a VESC on the CAN-bus responds.
 *
 * @param controller_id
 * The ID of the VESC.
 *
 * @param hw_type
 * The hardware type of the CAN device.
 *
 * @return
 * True for success, false otherwise.
 */
bool bms_can::can_ping(uint8_t controller_id, HW_TYPE *hw_type)
{

	queue_ping = xQueueCreate(10, sizeof(int32_t));
	int32_t ping_msg;
	uint8_t buffer[1];
	buffer[0] = CAN_ADDR;
	can_transmit_eid(controller_id |
						 ((uint32_t)CAN_PACKET_PING << 8),
					 buffer, 1);

	BLog_d(TAG, "ping waiting for pong...");

	int ret = xQueueReceive(queue_ping, &ping_msg, portMAX_DELAY);
	BLog_d(TAG, "ping waited for pong... %d", ret);

	vQueueDelete(queue_ping);
	queue_ping = NULL;

	if (ret = pdPASS)
	{
		if (hw_type)
		{
			*hw_type = ping_hw_last;
		}
	}

	return ret != 0;
}

void bms_can::can_transmit_eid(uint32_t id, const uint8_t *data, int len)
{
	if (len > 8)
	{
		len = 8;
	}

	twai_message_t txmsg = {0};
	// txmsg. = CAN_IDE_EXT;
	// txmsg.flags = CAN_MSG_FLAG_EXTD; deprecated
	// txmsg.flags = CAN_MSG_FLAG_RTR | CAN_MSG_FLAG_EXTD;
	txmsg.identifier = id;
	txmsg.extd = 1;
	txmsg.data_length_code = len;

	memcpy(&txmsg.data[0], &data[0], len);
	// BLog_i(TAG, "CAN tx id %x len %d", id, len);
	if (twai_transmit(&txmsg, pdMS_TO_TICKS(1000)) != ESP_OK)
	{
		BLog_i(TAG, "CAN tx failed");
		// return;

		uint32_t alerts = 0;
		BLog_i(TAG, "CAN reading alerts");
		twai_read_alerts(&alerts, pdMS_TO_TICKS(1000));
		BLog_i(TAG, "---Alert Read: -- : %04x", alerts);

		if (alerts & TWAI_ALERT_RECOVERY_IN_PROGRESS)
		{
			BLog_i(TAG, "Recovery in progress");
		}

		if (alerts & TWAI_ALERT_ABOVE_ERR_WARN)
		{
			BLog_i(TAG, "Surpassed Error Warning Limit");
		}

		if (alerts & TWAI_ALERT_ERR_PASS)
		{
			BLog_i(TAG, "Entered Error Passive state");
		}

		if (alerts & TWAI_ALERT_ERR_ACTIVE)
		{
			BLog_i(TAG, "Entered Can Error Active");
		}

		if (alerts & TWAI_ALERT_BUS_ERROR)
		{
			BLog_i(TAG, "Entered Alert Bus Error");
		}

		if (alerts & TWAI_ALERT_BUS_OFF)
		{
			BLog_d(TAG, "Bus Off --> Initiate bus recovery");
			twai_initiate_recovery(); // Needs 128 occurrences of bus free signal
		}

		// can_clear_transmit_queue();
		//	ESP_LOGE(tag, "--> Initiate bus recovery");
		// can_initiate_recovery();
		// can_start();
		//	ESP_LOGE(tag, " --> Returned from bus recovery");
		vTaskDelay(pdMS_TO_TICKS(1000));

		if (alerts & TWAI_ALERT_BUS_RECOVERED)
		{
			// Bus recovery was successful

			// only for testing. Does not help !!
			// esp_err_t res = can_reconfigure_alerts(alerts_enabled, NULL);
			BLog_i(TAG, "Bus Recovered"); // %d--> restarting Can", res);

			enable_alerts();

			// put can in start state again and re-enable alert monitoring
			twai_start();
		}
	}
	else
	{
		// BLog_i(TAG, "CAN tx success id %x len %d", id, len);
		tx_count++;
	}
	// BLog_i(TAG, "CAN tx end");
}
/*
	#ifdef CAN_DEBUG
				for (int i = 0; i < message.data_length_code; i++)
				{
					BLog_i(TAG, "byte %02x", message.data[i]);
				}
			}
			BLog_i(TAG, "\n");
#endif

	  //vTaskDelay(1000 / portTICK_PERIOD_MS);
	*/

void bms_can::can_status_task()
{
	vTaskDelay(pdMS_TO_TICKS(2000));

	for (;;)
	{
		int32_t send_index = 0;
		uint8_t buffer[8];

		// check the health of the bus
		twai_status_info_t status;
		twai_get_status_info(&status);
		BLog_i(TAG, " can_status_task %d/%d, rx-q:%d, tx-q:%d, rx-err:%d, tx-err:%d, arb-lost:%d, bus-err:%d, state: %s",
			   bms_can::rx_count, bms_can::tx_count,
			   status.msgs_to_rx, status.msgs_to_tx, status.rx_error_counter, status.tx_error_counter, status.arb_lost_count,
			   status.bus_error_count, ESP32_CAN_STATUS_STRINGS[status.state]);
		if (status.state == TWAI_STATE_STOPPED)
		{
			BLog_i(TAG, "CAN Driver is stopped!\n");

			// Start CAN driver
			/*
			if (can_start() == ESP_OK)
			{
				BLog_i(TAG, "CAN Driver started\n");
			}
			else
			{
				BLog_i(TAG, "Failed to start driver\n");
			}
			*/
		}

		// BLog_i(TAG, "can_status_task sending status v %f\n", bms_if_get_v_tot());

		buffer_append_float32_auto(buffer, bms_if_get_v_tot(), &send_index);
		buffer_append_float32_auto(buffer, bms_if_get_v_charge(), &send_index);
		can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_V_TOT << 8), buffer, send_index);

		// BLog_i(TAG, "can_status_task sending status i\n");
		send_index = 0;
		buffer_append_float32_auto(buffer, bms_if_get_i_in(), &send_index);
		buffer_append_float32_auto(buffer, bms_if_get_i_in_ic(), &send_index);
		can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_I << 8), buffer, send_index);

		// BLog_i(TAG, "can_status_task sending status wh\n");
		send_index = 0;
		buffer_append_float32_auto(buffer, bms_if_get_ah_cnt(), &send_index);
		buffer_append_float32_auto(buffer, bms_if_get_wh_cnt(), &send_index);
		can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_AH_WH << 8), buffer, send_index);

		// BLog_i(TAG, "can_status_task sending status cells\n");

		int cell_now = 0;
		int cell_max = (bms.getNumberCells());
		while (cell_now < cell_max)
		{
			send_index = 0;
			buffer[send_index++] = cell_now - 0;
			buffer[send_index++] = bms.getNumberCells();
			if (cell_now < cell_max)
			{
				buffer_append_float16(buffer, bms_if_get_v_cell(cell_now++), 1e3, &send_index);
			}
			if (cell_now < cell_max)
			{
				buffer_append_float16(buffer, bms_if_get_v_cell(cell_now++), 1e3, &send_index);
			}
			if (cell_now < cell_max)
			{
				buffer_append_float16(buffer, bms_if_get_v_cell(cell_now++), 1e3, &send_index);
			}
			// BLog_i(TAG, "can_status_task sending status cells %d %d\n", cell_now, cell_max);
			can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_V_CELL << 8), buffer, send_index);
		}

		// BLog_i(TAG, "can_status_task sending status balancing\n");
		send_index = 0;
		buffer[send_index++] = bms.getNumberCells();
		uint64_t bal_state = 0;
		for (int i = 0; i < cell_max; i++)
		{
			bal_state |= (uint64_t)bms_if_is_balancing_cell(i) << i;
		}
		buffer[send_index++] = (bal_state >> 48) & 0xFF;
		buffer[send_index++] = (bal_state >> 40) & 0xFF;
		buffer[send_index++] = (bal_state >> 32) & 0xFF;
		buffer[send_index++] = (bal_state >> 24) & 0xFF;
		buffer[send_index++] = (bal_state >> 16) & 0xFF;
		buffer[send_index++] = (bal_state >> 8) & 0xFF;
		buffer[send_index++] = (bal_state >> 0) & 0xFF;
		can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_BAL << 8), buffer, send_index);

		// BLog_i(TAG, "can_status_task sending status temp\n");
		int temp_now = 0;
		while (temp_now < HW_ADC_TEMP_SENSORS)
		{
			send_index = 0;
			buffer[send_index++] = temp_now;
			buffer[send_index++] = HW_ADC_TEMP_SENSORS;
			if (temp_now < HW_ADC_TEMP_SENSORS)
			{
				buffer_append_float16(buffer, bms_if_get_temp(temp_now++), 1e2, &send_index);
			}
			if (temp_now < HW_ADC_TEMP_SENSORS)
			{
				buffer_append_float16(buffer, bms_if_get_temp(temp_now++), 1e2, &send_index);
			}
			if (temp_now < HW_ADC_TEMP_SENSORS)
			{
				buffer_append_float16(buffer, bms_if_get_temp(temp_now++), 1e2, &send_index);
			}
			can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_TEMPS << 8), buffer, send_index);
		}
#ifdef BROKEN

		BLog_i(TAG, "can_status_task sending status hum\n");
		send_index = 0;
		buffer_append_float16(buffer, bms_if_get_humidity_sensor_temp(), 1e2, &send_index);
		buffer_append_float16(buffer, bms_if_get_humitidy(), 1e2, &send_index);
		buffer_append_float16(buffer, bms_if_get_temp_ic(), 1e2, &send_index); // Put IC temp here instead of making mew msg
		can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_HUM << 8), buffer, send_index);
#endif

		// BLog_i(TAG, "can_status_task sending status cell status\n");
		/*
		 * CAN_PACKET_BMS_SOC_SOH_TEMP_STAT
		 *
		 * b[0] - b[1]: V_CELL_MIN (mV)
		 * b[2] - b[3]: V_CELL_MAX (mV)
		 * b[4]: SoC (0 - 255)
		 * b[5]: SoH (0 - 255)
		 * b[6]: T_CELL_MAX (-128 to +127 degC)
		 * b[7]: State bitfield:
		 * [B7      B6      B5      B4      B3      B2      B1      B0      ]
		 * [RSV     RSV     RSV     RSV     RSV     CHG_OK  IS_BAL  IS_CHG  ]
		 */
		send_index = 0;
		buffer_append_float16(buffer, bms_if_get_v_cell_min(), 1e3, &send_index);
		buffer_append_float16(buffer, bms_if_get_v_cell_max(), 1e3, &send_index);

		buffer[send_index++] = (uint8_t)(bms_if_get_soc() * 255.0);
		buffer[send_index++] = (uint8_t)(bms_if_get_soh() * 255.0);
		buffer[send_index++] = (int8_t)HW_TEMP_CELLS_MAX();
		buffer[send_index++] =
			((bms_if_is_charging() ? 1 : 0) << 0) |
			((bms_if_is_balancing() ? 1 : 0) << 1) |
			((bms_if_is_charge_allowed() ? 1 : 0) << 2);
		can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_SOC_SOH_TEMP_STAT << 8), buffer, send_index);

		// Diebie
		buffer_append_float32_auto(buffer, bms_if_get_v_tot(), &send_index);
		buffer_append_float32_auto(buffer, bms_if_get_i_in(), &send_index);
		can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_STATUS_MAIN_IV << 8), buffer, send_index);

		buffer_append_float32_auto(buffer, bms_if_get_v_cell_min(), &send_index);
		buffer_append_float32_auto(buffer, bms_if_get_v_cell_max(), &send_index);
		can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_STATUS_CELLVOLTAGE << 8), buffer, send_index);

		buffer_append_float16(buffer, bms_if_get_v_tot(), 1e2, &send_index);
		buffer_append_float16(buffer, bms.getRemainingCapacityMah() / 1000.0, 1e2, &send_index);
		buffer[send_index++] = (uint8_t)(bms_if_get_soc() * 100.0);
		buffer[send_index++] = (uint8_t)0;
		buffer[send_index++] = (uint8_t)0;
		buffer[send_index++] = (uint8_t)0;
		can_transmit_eid(CAN_ADDR | ((uint32_t)CAN_PACKET_BMS_STATUS_THROTTLE_CH_DISCH_BOOL << 8), buffer, send_index);

		// TODO: allow config
		int32_t sleep_time = 1000 / 1;
		if (sleep_time == 0)
		{
			sleep_time = 1;
		}

		// chThdSleep(sleep_time);
		// delay(sleep_time);
		// vTaskDelay(sleep_time / portTICK_PERIOD_MS);
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

float bms_can::bms_if_get_v_tot()
{
	return (float)bms.getVoltageMv() / 1000.0;
}

float bms_can::bms_if_get_v_charge()
{
	return (float)bms.getChargeVoltage() / 1000.0;
}

float bms_can::bms_if_get_i_in()
{
	return (float)bms.getCurrentMa() / 1000.0;
}

float bms_can::bms_if_get_i_in_ic()
{
	return (float)bms.getCurrentInstantMa() / 1000.0;
}

float bms_can::bms_if_get_ah_cnt()
{
	return (float)bms.getRemainingCapacityMah();
}

float bms_can::bms_if_get_wh_cnt()
{
	return (float)bms.getRemainingCapacityMah() / 1000.0 * (float)bms.getVoltageMv() / 1000.0;
}

float bms_can::bms_if_get_v_cell(int cell)
{
	return (float)bms.getCellVoltageMv(cell) / 1000.0;
}

// Return range for soc/soh is 0.0 -> 1.0
float bms_can::bms_if_get_soc()
{
	return (float)bms.getSocPct() / 100.0;
}

float bms_can::bms_if_get_soh()
{
	return (float)bms.getSohPct() / 100.0;
}

bool bms_can::bms_if_is_charging()
{
	return false; // bms.isCharging();
}

bool bms_can::bms_if_is_charge_allowed()
{
	return true;
}

bool bms_can::bms_if_is_balancing()
{
	return false; // bms.isBalancing();
}

bool bms_can::bms_if_is_balancing_cell(int cell)
{
	return false; // bms.isCellBalancing(cell);
}

float bms_can::bms_if_get_v_cell_min()
{
	return (float)bms.getCellVoltageMinMv(0) / 1000.0;
}

float bms_can::bms_if_get_v_cell_max()
{
	return (float)bms.getCellVoltageMaxMv(0) / 1000.0;
}

float bms_can::bms_if_get_v_cell_avg()
{
	return (float)bms.getVoltageMv() * 1000.0 / bms.getNumberCells();
}

float bms_can::bms_if_get_humidity_sensor_temp()
{
	return 31.4;
}

float bms_can::bms_if_get_humitidy()
{
	return 31.4;
}

float bms_can::bms_if_get_temp_ic()
{
	return (float)bms.getChipTempC();
	;
}

int bms_can::HW_TEMP_CELLS_MAX()
{
	return (float)bms.getMaxCellTemp();
}

float bms_can::bms_if_get_temp(int sensor)
{
	if (sensor == 0)
	{
		return (float)bms.getMinCellTemp();
	}
	else
	{
		return (float)bms.getMaxCellTemp();
	}
}

// uint8_t totalNumberOfBanks;
//   uint8_t totalNumberOfSeriesModules;

int bms_can::rxcnt(void)
{
	return bms_can::rx_count;
}

int bms_can::txcnt(void)
{
	return bms_can::tx_count;
}
