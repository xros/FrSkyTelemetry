#include "FrSkyProcessor.h"


#define TELEM_TEXT_MESSAGE_MAX                      32
#define TELEM_NUM_BUFFERS                            4

char telem_text_message_data_buffer[TELEM_NUM_BUFFERS][TELEM_TEXT_MESSAGE_MAX];
uint8_t telem_text_message_index = 0;
uint32_t telem_message_expiry = 0L;
uint16_t message_packet_sequence = 0;
uint16_t current_message_number = 0;
uint16_t next_message_number = 1;

void frsky_send_text_message(char *msg) {
	uint8_t c;
	uint16_t dst_index = 0;
	for (uint8_t i = 0; i < strlen(msg); i++) {
		c = msg[i];
		if (c >= 32 && c <= 126) {
			telem_text_message_data_buffer[next_message_number % TELEM_NUM_BUFFERS][dst_index++] = c;
		}
	}
	telem_text_message_data_buffer[next_message_number % TELEM_NUM_BUFFERS][dst_index++] = 0;
	next_message_number++;
}

void telem_load_next_buffer() {
	if (millis() > telem_message_expiry) {
		if ((current_message_number + 1) < next_message_number) {
			telem_message_expiry = millis() + 2000;
			current_message_number++;
		}
		else {
			telem_text_message_data_buffer[current_message_number % TELEM_NUM_BUFFERS][0] = '\0';
			telem_message_expiry = millis(); // No reason to keep blank up for a set time
		}
	}
	telem_text_message_index = 0;
}

void telem_load_next_bufferNEWWWWWWWWWWWWWWWWWWWWWW() {
	if ((current_message_number + 1) < next_message_number) {

		current_message_number++;
	}
	else {
		telem_text_message_data_buffer[current_message_number % TELEM_NUM_BUFFERS][0] = '\0';
	}
	telem_text_message_index = 0;
}

char frsky_get_next_message_byte() {
	if (current_message_number == next_message_number) {
		return '\0';
	}
	else if (telem_text_message_index >= strlen(telem_text_message_data_buffer[current_message_number % TELEM_NUM_BUFFERS])) {
		return '\0';
	}
	else {
		return telem_text_message_data_buffer[current_message_number % TELEM_NUM_BUFFERS][telem_text_message_index++] & 0x7f;
	}
}

uint16_t telem_text_get_word() { // LSB is lost so use upper 15 bits
	uint16_t data_word;
	char ch, cl;
	ch = frsky_get_next_message_byte();
	data_word = ch << 8;
	cl = frsky_get_next_message_byte();
	data_word |= cl << 1;
	data_word |= (message_packet_sequence++ & 1) << 15; // MSB will change on each telemetry packet so rx knows to update message
	if (ch == '\0' || cl == '\0') {
		telem_load_next_bufferNEWWWWWWWWWWWWWWWWWWWWWW();
	}
	return data_word;
}

// ***********************************************************************
FrSkyProcessor::FrSkyProcessor(const SerialId& serial_pin, uint8_t fault_pin) :
seq_last_slow_sent {BASE_MODE},
softSerial {serial_pin, serial_pin, true},
fault_pin {fault_pin},
crc {0}, // used for crc calc of frsky-packet
lastRx {0},
variometer_index {0},
serial_id {serial_pin}
{

	softSerial.begin(BAUD_RATE);
	//frsky_send_text_message("Telemetry Started");
}

FrSkyProcessor::~FrSkyProcessor() {
	//delete softSerial;
}

void FrSkyProcessor::process(Telemetry& mav_telemetry, bool new_data) {
	uint8_t data = 0;

	while(softSerial.available()){
		data = softSerial.read();
		if (lastRx == START_STOP) {
			if (new_data == true)
				digitalWrite(fault_pin, HIGH);
			if (data == SENSOR_ID_VARIO || data == SENSOR_ID_FAS ||
				data == SENSOR_ID_GPS) {
				uint32_t latlong = 0;
				switch(++variometer_index % 13) {
				case 0:
					sendPackage(DATA_FRAME, FR_ID_VARIO, mav_telemetry.climb_rate);
					break;
				case 1:
					sendPackage(DATA_FRAME, FR_ID_ALTITUDE, mav_telemetry.bar_altitude * 100);
					break;
				case 2:
					sendPackage(DATA_FRAME, FR_ID_VFAS, mav_telemetry.battery_voltage);
					break;
				case 3:
					sendPackage(DATA_FRAME, FR_ID_CURRENT, mav_telemetry.battery_current);
					break;
				case 4:
					sendPackage(DATA_FRAME, FR_ID_SPEED, mav_telemetry.groundspeed * 36); // from GPS converted to km/h
					break;
				case 5: // Sends the ap_longitude value, setting bit 31 high
					if (mav_telemetry.gps_longitude < 0)
						latlong = (((mav_telemetry.gps_longitude) / -100) * 6) | 0xC0000000;
					else
						latlong = (((mav_telemetry.gps_longitude) / 100) * 6) | 0x80000000;
					sendPackage(DATA_FRAME, FR_ID_LATLONG, latlong);
					break;
				case 6: // Sends the ap_latitude value, setting bit 31 low
					if (mav_telemetry.gps_latitude < 0)
						latlong = (((mav_telemetry.gps_latitude) / -100) * 6) | 0x40000000;
					else
						latlong = (((mav_telemetry.gps_latitude) / 100) * 6);
					sendPackage(DATA_FRAME, FR_ID_LATLONG, latlong);
					break;
				case 7:
					sendPackage(DATA_FRAME, FR_ID_GPS_ALT, mav_telemetry.gps_altitude / 10); // from GPS,  100=1m
					break;
				case 8:
					sendPackage(DATA_FRAME, FR_ID_HEADING, mav_telemetry.heading * 100);
					break;
				case 9:
					sendPackage(DATA_FRAME, FR_ID_ROLL, mav_telemetry.roll);
					break;
				case 10:
					sendPackage(DATA_FRAME, FR_ID_PITCH, mav_telemetry.pitch);
					break;
				case 11:
				{
					int16_t hdop_val;
					hdop_val = mav_telemetry.gps_hdop / 4;
					if (hdop_val > 249)
						hdop_val = 249;
					sendPackage(DATA_FRAME, FR_ID_ADC2, hdop_val); //  value must be between 0 and 255.  1 produces 0.4
				}
					break;
				case 12:
					sendSlowParameters(mav_telemetry);
					break;
				default:
					break;
				}
			}
			else if (data == SENSOR_ID_RPM) {
				uint16_t data_word;
				data_word = telem_text_get_word();
				sendPackage(DATA_FRAME, FR_ID_RPM, data_word);
			}
		}
		lastRx = data;
	}
}

//Slow changing but probably urgent.

void FrSkyProcessor::sendSlowParameters(Telemetry& mav_telemetry) {
	SlowParameters selected = INVALID_PARAMETER;
	for (int i = 0; i < INVALID_PARAMETER; i++) {
		if ((mav_telemetry.slow_changed_parameter >> i) & 0x1) {
			selected = (SlowParameters) i;
			mav_telemetry.slow_changed_parameter &= ~(1 << i);
			break;
		}
	}

	if (selected == INVALID_PARAMETER) {
		seq_last_slow_sent =
			(SlowParameters) ((seq_last_slow_sent + 1) % INVALID_PARAMETER);
		selected = seq_last_slow_sent;
	}
	switch(selected) {
	case BASE_MODE:
		sendPackage(DATA_FRAME, FR_ID_BASE_MODE, mav_telemetry.base_mode);
		break;
	case CUSTOM_MODE:
		sendPackage(DATA_FRAME, FR_ID_CUSTOM_MODE, mav_telemetry.custom_mode);
		break;
	case GPS_FIX_TYPE_AND_SAT_VISIBLE:
	{
		int32_t gps_status = (mav_telemetry.gps_satellites_visible * 10) +
			mav_telemetry.gps_fixtype;
		sendPackage(DATA_FRAME, FR_ID_T1, gps_status);
	}
		break;
	case HDOP_EX:
		sendPackage(DATA_FRAME, FR_ID_HDOP_EX, mav_telemetry.gps_hdop);
		break;
	case SYSTEM_STATUS:
		sendPackage(DATA_FRAME, FR_ID_SYSTEM_STATUS, mav_telemetry.system_status);
		break;
	case MAV_TYPE:
		sendPackage(DATA_FRAME, FR_ID_MAV_TYPE, mav_telemetry.mav_type);
		break;
	case BATTERY_REMAINING:
		sendPackage(DATA_FRAME, FR_ID_BAT_REMAINING, mav_telemetry.battery_remaining * 100);
		break;
	case INVALID_PARAMETER:
		break;
	}
}

void FrSkyProcessor::sendByte(uint8_t byte) {
	if (byte == 0x7E) {
		softSerial.write(0x7D);
		softSerial.write(0x5E);
	}
	else if (byte == 0x7D) {
		softSerial.write(0x7D);
		softSerial.write(0x5D);
	}
	else {
		softSerial.write(byte);
	}
	//softSerial.write(byte);
	// CRC update
	crc += byte; //0-1FF
	crc += crc >> 8; //0-100
	crc &= 0x00ff;
	crc += crc >> 8; //0-0FF
	crc &= 0x00ff;
}

void FrSkyProcessor::sendCrc() {
	softSerial.write(0xFF - crc);
	crc = 0; // CRC reset
}

void FrSkyProcessor::sendPackage(uint8_t header, uint16_t id, uint32_t value) {

	pinMode(serial_id, OUTPUT);
	sendByte(header);
	uint8_t *bytes = (uint8_t*) & id;
	sendByte(bytes[0]);
	sendByte(bytes[1]);
	bytes = (uint8_t*) & value;
	sendByte(bytes[0]);
	sendByte(bytes[1]);
	sendByte(bytes[2]);
	sendByte(bytes[3]);
	sendCrc();
	softSerial.flush();
	pinMode(serial_id, INPUT);
	digitalWrite(fault_pin, LOW);
}
