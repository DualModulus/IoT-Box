/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
  (C)2013 Semtech-Cycleo

Description:
	Configure Lora concentrator and forward packets to a server

License: Revised BSD License, see LICENSE.TXT file include in the project
Maintainer: Sylvain Miermont
*/


/* -------------------------------------------------------------------------- */
/* --- DEPENDANCIES --------------------------------------------------------- */

/* fix an issue between POSIX and C99 */
#if __STDC_VERSION__ >= 199901L
	#define _XOPEN_SOURCE 600
#else
	#define _XOPEN_SOURCE 500
#endif


#include <stdint.h>		/* C99 types */
#include <stdbool.h>	/* bool type */
#include <stdio.h>		/* printf, fprintf, snprintf, fopen, fputs */

#include <string.h>		/* memset */
#include <signal.h>		/* sigaction */
#include <time.h>		/* time, clock_gettime, strftime, gmtime */
#include <sys/time.h>	/* timeval */
#include <unistd.h>		/* getopt, access */
#include <stdlib.h>		/* atoi, exit */
#include <errno.h>		/* error messages */

#include <pthread.h>

#include "parson.h"
#include "base64.h"
#include "aes.h"
#include "cmac.h"
#include "utilities.h"
#include "sqlite3.h"

#include "loragw_hal.h"
#include "loragw_aux.h"

/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))
#define STRINGIFY(x)	#x
#define STR(x)			STRINGIFY(x)
#define MSG(args...)	printf(args) /* message that is destined to the user */
#define TRACE() 		fprintf(stderr, "@ %s %d\n", __FUNCTION__, __LINE__);

/* -------------------------------------------------------------------------- */
/* --- PRIVATE CONSTANTS ---------------------------------------------------- */

#ifndef VERSION_STRING
  #define VERSION_STRING "undefined"
#endif

#define DEFAULT_STAT		30	/* default time interval for statistics */
#define PUSH_TIMEOUT_MS		100
#define FETCH_SLEEP_MS		10	/* nb of ms waited when a fetch return no packets */

#define PKT_PUSH_DATA	0
#define PKT_PUSH_ACK	1
#define PKT_PULL_DATA	2
#define PKT_PULL_RESP	3
#define PKT_PULL_ACK	4

#define NB_PKT_MAX		8 /* max number of packets per fetch/send cycle */

#define MIN_LORA_PREAMB	6 /* minimum Lora preamble length for this application */
#define STD_LORA_PREAMB	8
#define MIN_FSK_PREAMB	3 /* minimum FSK preamble length for this application */
#define STD_FSK_PREAMB	4

/* -------------------------------------------------------------------------- */
/* --- PRIVATE VARIABLES (GLOBAL) ------------------------------------------- */

/* signal handling variables */
volatile bool exit_sig = false; /* 1 -> application terminates cleanly (shut down hardware, close open files, etc) */
volatile bool quit_sig = false; /* 1 -> application terminates without shutting down the hardware */

/* packets filtering configuration variables */
static bool fwd_valid_pkt = true; /* packets with PAYLOAD CRC OK are forwarded */
static bool fwd_error_pkt = false; /* packets with PAYLOAD CRC ERROR are NOT forwarded */
static bool fwd_nocrc_pkt = false; /* packets with NO PAYLOAD CRC are NOT forwarded */

/* network configuration variables */
static uint64_t lgwm = 0; /* Lora gateway MAC address */

/* statistics collection configuration variables */
static unsigned stat_interval = DEFAULT_STAT; /* time interval (in sec) at which statistics are collected and displayed */

/* hardware access control and correction */
static pthread_mutex_t mx_concent = PTHREAD_MUTEX_INITIALIZER; /* control access to the concentrator */

/* measurements to establish statistics */
static pthread_mutex_t mx_meas_up = PTHREAD_MUTEX_INITIALIZER; /* control access to the upstream measurements */
static uint32_t meas_nb_rx_rcv = 0; /* count packets received */
static uint32_t meas_nb_rx_ok = 0; /* count packets received with PAYLOAD CRC OK */
static uint32_t meas_nb_rx_bad = 0; /* count packets received with PAYLOAD CRC ERROR */
static uint32_t meas_nb_rx_nocrc = 0; /* count packets received with NO PAYLOAD CRC */
static uint32_t meas_up_pkt_fwd = 0; /* number of radio packet forwarded to the server */
static uint32_t meas_up_network_byte = 0; /* sum of UDP bytes sent for upstream traffic */
static uint32_t meas_up_payload_byte = 0; /* sum of radio payload bytes sent for upstream traffic */
static uint32_t meas_up_dgram_sent = 0; /* number of datagrams sent for upstream traffic */
static uint32_t meas_up_ack_rcv = 0; /* number of datagrams acknowledged for upstream traffic */

/* auto-quit function */
static uint32_t autoquit_threshold = 0; /* enable auto-quit after a number of non-acknowledged PULL_DATA (0 = disabled)*/

/*!
 * AES encryption/decryption cipher application session key
 */
static const uint8_t LoRaMacAppSKey[] =
{
    0xAF, 0xBE, 0xCD, 0x56, 0x47, 0x38, 0x29, 0x10,
    0x01, 0x92, 0x83, 0x74, 0x65, 0xFA, 0xEB, 0xDC
};

/*!
 * Encryption aBlock and sBlock
 */
static uint8_t aBlock[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                          };
static uint8_t sBlock[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                          };

/*!
 * AES computation context variable
 */
static aes_context AesContext;

union u {
	float f;
	uint8_t b[sizeof(float)];
};
/* -------------------------------------------------------------------------- */
/* --- PRIVATE FUNCTIONS DECLARATION ---------------------------------------- */

static void sig_handler(int sigio);

static int parse_SX1301_configuration(const char * conf_file);

static int parse_gateway_configuration(const char * conf_file);

void LoRaMacPayloadEncrypt( const uint8_t *buffer, uint16_t size, const uint8_t *key, uint32_t address, uint8_t dir, uint32_t sequenceCounter, uint8_t *encBuffer );

void LoRaMacPayloadDecrypt( const uint8_t *buffer, uint16_t size, const uint8_t *key, uint32_t address, uint8_t dir, uint32_t sequenceCounter, uint8_t *decBuffer );

/* threads */
void thread_up(void);

/* -------------------------------------------------------------------------- */
/* --- PRIVATE FUNCTIONS DEFINITION ----------------------------------------- */

static int callback(void *data, int argc, char **argv, char **azColName)
{
	int i;
	fprintf(stderr, "%s: \n", (const char*)data);
	for(i=0; i<argc; i++){
		printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}
	printf("\n");
	return 0;
}

void LoRaMacPayloadEncrypt( const uint8_t *buffer, uint16_t size, const uint8_t *key, uint32_t address, uint8_t dir, uint32_t sequenceCounter, uint8_t *encBuffer )
{
	uint16_t i;
	uint8_t bufferIndex = 0;
	uint16_t ctr = 1;

	memset1( AesContext.ksch, '\0', 240 );
	aes_set_key( key, 16, &AesContext );

	aBlock[5] = dir;

	aBlock[6] = ( address ) & 0xFF;
	aBlock[7] = ( address >> 8 ) & 0xFF;
	aBlock[8] = ( address >> 16 ) & 0xFF;
	aBlock[9] = ( address >> 24 ) & 0xFF;

	aBlock[10] = ( sequenceCounter ) & 0xFF;
	aBlock[11] = ( sequenceCounter >> 8 ) & 0xFF;
	aBlock[12] = ( sequenceCounter >> 16 ) & 0xFF;
	aBlock[13] = ( sequenceCounter >> 24 ) & 0xFF;

	while( size >= 16 )
	{
		aBlock[15] = ( ( ctr ) & 0xFF );
		ctr++;
		aes_encrypt( aBlock, sBlock, &AesContext );
		for( i = 0; i < 16; i++ )
		{
			encBuffer[bufferIndex + i] = buffer[bufferIndex + i] ^ sBlock[i];
		}
		size -= 16;
		bufferIndex += 16;
	}

	if( size > 0 )
	{
		aBlock[15] = ( ( ctr ) & 0xFF );
		aes_encrypt( aBlock, sBlock, &AesContext );
		for( i = 0; i < size; i++ )
		{
			encBuffer[bufferIndex + i] = buffer[bufferIndex + i] ^ sBlock[i];
		}
	}
}

void LoRaMacPayloadDecrypt( const uint8_t *buffer, uint16_t size, const uint8_t *key, uint32_t address, uint8_t dir, uint32_t sequenceCounter, uint8_t *decBuffer )
{
	LoRaMacPayloadEncrypt( buffer, size, key, address, dir, sequenceCounter, decBuffer );
}

static void sig_handler(int sigio) {
	if (sigio == SIGQUIT) {
		quit_sig = true;;
	} else if ((sigio == SIGINT) || (sigio == SIGTERM)) {
		exit_sig = true;
	}
	return;
}

static int parse_SX1301_configuration(const char * conf_file) {
	int i;
	char param_name[32]; /* used to generate variable parameter names */
	const char *str; /* used to store string value from JSON object */
	const char conf_obj_name[] = "SX1301_conf";
	JSON_Value *root_val = NULL;
	JSON_Object *conf_obj = NULL;
	JSON_Value *val = NULL;
	struct lgw_conf_board_s boardconf;
	struct lgw_conf_rxrf_s rfconf;
	struct lgw_conf_rxif_s ifconf;
	uint32_t sf, bw, fdev;
	struct lgw_tx_gain_lut_s txlut;

	/* try to parse JSON */
	root_val = json_parse_file_with_comments(conf_file);
	if (root_val == NULL) {
		MSG("ERROR: %s is not a valid JSON file\n", conf_file);
		exit(EXIT_FAILURE);
	}

	/* point to the gateway configuration object */
	conf_obj = json_object_get_object(json_value_get_object(root_val), conf_obj_name);
	if (conf_obj == NULL) {
		MSG("INFO: %s does not contain a JSON object named %s\n", conf_file, conf_obj_name);
		return -1;
	} else {
		MSG("INFO: %s does contain a JSON object named %s, parsing SX1301 parameters\n", conf_file, conf_obj_name);
	}

	/* set board configuration */
	memset(&boardconf, 0, sizeof boardconf); /* initialize configuration structure */
	val = json_object_get_value(conf_obj, "lorawan_public"); /* fetch value (if possible) */
	if (json_value_get_type(val) == JSONBoolean) {
		boardconf.lorawan_public = (bool)json_value_get_boolean(val);
	} else {
		MSG("WARNING: Data type for lorawan_public seems wrong, please check\n");
		boardconf.lorawan_public = false;
	}
	val = json_object_get_value(conf_obj, "clksrc"); /* fetch value (if possible) */
	if (json_value_get_type(val) == JSONNumber) {
		boardconf.clksrc = (uint8_t)json_value_get_number(val);
	} else {
		MSG("WARNING: Data type for clksrc seems wrong, please check\n");
		boardconf.clksrc = 0;
	}
	MSG("INFO: lorawan_public %d, clksrc %d\n", boardconf.lorawan_public, boardconf.clksrc);
	/* all parameters parsed, submitting configuration to the HAL */
        if (lgw_board_setconf(boardconf) != LGW_HAL_SUCCESS) {
                MSG("WARNING: Failed to configure board\n");
	}

	/* set configuration for tx gains */
	memset(&txlut, 0, sizeof txlut); /* initialize configuration structure */
	for (i = 0; i < TX_GAIN_LUT_SIZE_MAX; i++) {
		snprintf(param_name, sizeof param_name, "tx_lut_%i", i); /* compose parameter path inside JSON structure */
		val = json_object_get_value(conf_obj, param_name); /* fetch value (if possible) */
		if (json_value_get_type(val) != JSONObject) {
			MSG("INFO: no configuration for tx gain lut %i\n", i);
			continue;
		}
		txlut.size++; /* update TX LUT size based on JSON object found in configuration file */
		/* there is an object to configure that TX gain index, let's parse it */
		snprintf(param_name, sizeof param_name, "tx_lut_%i.pa_gain", i);
		val = json_object_dotget_value(conf_obj, param_name);
		if (json_value_get_type(val) == JSONNumber) {
			txlut.lut[i].pa_gain = (uint8_t)json_value_get_number(val);
		} else {
			MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
			txlut.lut[i].pa_gain = 0;
		}
                snprintf(param_name, sizeof param_name, "tx_lut_%i.dac_gain", i);
                val = json_object_dotget_value(conf_obj, param_name);
                if (json_value_get_type(val) == JSONNumber) {
                        txlut.lut[i].dac_gain = (uint8_t)json_value_get_number(val);
                } else {
                        txlut.lut[i].dac_gain = 3; /* This is the only dac_gain supported for now */
                }
                snprintf(param_name, sizeof param_name, "tx_lut_%i.dig_gain", i);
                val = json_object_dotget_value(conf_obj, param_name);
                if (json_value_get_type(val) == JSONNumber) {
                        txlut.lut[i].dig_gain = (uint8_t)json_value_get_number(val);
                } else {
			MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
                        txlut.lut[i].dig_gain = 0;
                }
                snprintf(param_name, sizeof param_name, "tx_lut_%i.mix_gain", i);
                val = json_object_dotget_value(conf_obj, param_name);
                if (json_value_get_type(val) == JSONNumber) {
                        txlut.lut[i].mix_gain = (uint8_t)json_value_get_number(val);
                } else {
			MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
                        txlut.lut[i].mix_gain = 0;
                }
                snprintf(param_name, sizeof param_name, "tx_lut_%i.rf_power", i);
                val = json_object_dotget_value(conf_obj, param_name);
                if (json_value_get_type(val) == JSONNumber) {
                        txlut.lut[i].rf_power = (int8_t)json_value_get_number(val);
                } else {
			MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
                        txlut.lut[i].rf_power = 0;
                }
	}
	/* all parameters parsed, submitting configuration to the HAL */
	MSG("INFO: Configuring TX LUT with %u indexes\n", txlut.size);
        if (lgw_txgain_setconf(&txlut) != LGW_HAL_SUCCESS) {
                MSG("WARNING: Failed to configure concentrator TX Gain LUT\n");
	}

	/* set configuration for RF chains */
	for (i = 0; i < LGW_RF_CHAIN_NB; ++i) {
		memset(&rfconf, 0, sizeof rfconf); /* initialize configuration structure */
		snprintf(param_name, sizeof param_name, "radio_%i", i); /* compose parameter path inside JSON structure */
		val = json_object_get_value(conf_obj, param_name); /* fetch value (if possible) */
		if (json_value_get_type(val) != JSONObject) {
			MSG("INFO: no configuration for radio %i\n", i);
			continue;
		}
		/* there is an object to configure that radio, let's parse it */
		snprintf(param_name, sizeof param_name, "radio_%i.enable", i);
		val = json_object_dotget_value(conf_obj, param_name);
		if (json_value_get_type(val) == JSONBoolean) {
			rfconf.enable = (bool)json_value_get_boolean(val);
		} else {
			rfconf.enable = false;
		}
		if (rfconf.enable == false) { /* radio disabled, nothing else to parse */
			MSG("INFO: radio %i disabled\n", i);
		} else  { /* radio enabled, will parse the other parameters */
			snprintf(param_name, sizeof param_name, "radio_%i.freq", i);
			rfconf.freq_hz = (uint32_t)json_object_dotget_number(conf_obj, param_name);
			snprintf(param_name, sizeof param_name, "radio_%i.rssi_offset", i);
			rfconf.rssi_offset = (float)json_object_dotget_number(conf_obj, param_name);
			snprintf(param_name, sizeof param_name, "radio_%i.type", i);
			str = json_object_dotget_string(conf_obj, param_name);
			if (!strncmp(str, "SX1255", 6)) {
				rfconf.type = LGW_RADIO_TYPE_SX1255;
			} else if (!strncmp(str, "SX1257", 6)) {
				rfconf.type = LGW_RADIO_TYPE_SX1257;
			} else {
				MSG("WARNING: invalid radio type: %s (should be SX1255 or SX1257)\n", str);
			}
			snprintf(param_name, sizeof param_name, "radio_%i.tx_enable", i);
			val = json_object_dotget_value(conf_obj, param_name);
			if (json_value_get_type(val) == JSONBoolean) {
				rfconf.tx_enable = (bool)json_value_get_boolean(val);
			} else {
				rfconf.tx_enable = false;
			}
			MSG("INFO: radio %i enabled (type %s), center frequency %u, RSSI offset %f, tx enabled %d\n", i, str, rfconf.freq_hz, rfconf.rssi_offset, rfconf.tx_enable);
		}
		/* all parameters parsed, submitting configuration to the HAL */
		if (lgw_rxrf_setconf(i, rfconf) != LGW_HAL_SUCCESS) {
			MSG("WARNING: invalid configuration for radio %i\n", i);
		}
	}

	/* set configuration for Lora multi-SF channels (bandwidth cannot be set) */
	for (i = 0; i < LGW_MULTI_NB; ++i) {
		memset(&ifconf, 0, sizeof ifconf); /* initialize configuration structure */
		snprintf(param_name, sizeof param_name, "chan_multiSF_%i", i); /* compose parameter path inside JSON structure */
		val = json_object_get_value(conf_obj, param_name); /* fetch value (if possible) */
		if (json_value_get_type(val) != JSONObject) {
			MSG("INFO: no configuration for Lora multi-SF channel %i\n", i);
			continue;
		}
		/* there is an object to configure that Lora multi-SF channel, let's parse it */
		snprintf(param_name, sizeof param_name, "chan_multiSF_%i.enable", i);
		val = json_object_dotget_value(conf_obj, param_name);
		if (json_value_get_type(val) == JSONBoolean) {
			ifconf.enable = (bool)json_value_get_boolean(val);
		} else {
			ifconf.enable = false;
		}
		if (ifconf.enable == false) { /* Lora multi-SF channel disabled, nothing else to parse */
			MSG("INFO: Lora multi-SF channel %i disabled\n", i);
		} else  { /* Lora multi-SF channel enabled, will parse the other parameters */
			snprintf(param_name, sizeof param_name, "chan_multiSF_%i.radio", i);
			ifconf.rf_chain = (uint32_t)json_object_dotget_number(conf_obj, param_name);
			snprintf(param_name, sizeof param_name, "chan_multiSF_%i.if", i);
			ifconf.freq_hz = (int32_t)json_object_dotget_number(conf_obj, param_name);
			// TODO: handle individual SF enabling and disabling (spread_factor)
			MSG("INFO: Lora multi-SF channel %i>  radio %i, IF %i Hz, 125 kHz bw, SF 7 to 12\n", i, ifconf.rf_chain, ifconf.freq_hz);
		}
		/* all parameters parsed, submitting configuration to the HAL */
		if (lgw_rxif_setconf(i, ifconf) != LGW_HAL_SUCCESS) {
			MSG("WARNING: invalid configuration for Lora multi-SF channel %i\n", i);
		}
	}

	/* set configuration for Lora standard channel */
	memset(&ifconf, 0, sizeof ifconf); /* initialize configuration structure */
	val = json_object_get_value(conf_obj, "chan_Lora_std"); /* fetch value (if possible) */
	if (json_value_get_type(val) != JSONObject) {
		MSG("INFO: no configuration for Lora standard channel\n");
	} else {
		val = json_object_dotget_value(conf_obj, "chan_Lora_std.enable");
		if (json_value_get_type(val) == JSONBoolean) {
			ifconf.enable = (bool)json_value_get_boolean(val);
		} else {
			ifconf.enable = false;
		}
		if (ifconf.enable == false) {
			MSG("INFO: Lora standard channel %i disabled\n", i);
		} else  {
			ifconf.rf_chain = (uint32_t)json_object_dotget_number(conf_obj, "chan_Lora_std.radio");
			ifconf.freq_hz = (int32_t)json_object_dotget_number(conf_obj, "chan_Lora_std.if");
			bw = (uint32_t)json_object_dotget_number(conf_obj, "chan_Lora_std.bandwidth");
			switch(bw) {
				case 500000: ifconf.bandwidth = BW_500KHZ; break;
				case 250000: ifconf.bandwidth = BW_250KHZ; break;
				case 125000: ifconf.bandwidth = BW_125KHZ; break;
				default: ifconf.bandwidth = BW_UNDEFINED;
			}
			sf = (uint32_t)json_object_dotget_number(conf_obj, "chan_Lora_std.spread_factor");
			switch(sf) {
				case  7: ifconf.datarate = DR_LORA_SF7;  break;
				case  8: ifconf.datarate = DR_LORA_SF8;  break;
				case  9: ifconf.datarate = DR_LORA_SF9;  break;
				case 10: ifconf.datarate = DR_LORA_SF10; break;
				case 11: ifconf.datarate = DR_LORA_SF11; break;
				case 12: ifconf.datarate = DR_LORA_SF12; break;
				default: ifconf.datarate = DR_UNDEFINED;
			}
			MSG("INFO: Lora std channel> radio %i, IF %i Hz, %u Hz bw, SF %u\n", ifconf.rf_chain, ifconf.freq_hz, bw, sf);
		}
		if (lgw_rxif_setconf(8, ifconf) != LGW_HAL_SUCCESS) {
			MSG("WARNING: invalid configuration for Lora standard channel\n");
		}
	}

	/* set configuration for FSK channel */
	memset(&ifconf, 0, sizeof ifconf); /* initialize configuration structure */
	val = json_object_get_value(conf_obj, "chan_FSK"); /* fetch value (if possible) */
	if (json_value_get_type(val) != JSONObject) {
		MSG("INFO: no configuration for FSK channel\n");
	} else {
		val = json_object_dotget_value(conf_obj, "chan_FSK.enable");
		if (json_value_get_type(val) == JSONBoolean) {
			ifconf.enable = (bool)json_value_get_boolean(val);
		} else {
			ifconf.enable = false;
		}
		if (ifconf.enable == false) {
			MSG("INFO: FSK channel %i disabled\n", i);
		} else  {
			ifconf.rf_chain = (uint32_t)json_object_dotget_number(conf_obj, "chan_FSK.radio");
			ifconf.freq_hz = (int32_t)json_object_dotget_number(conf_obj, "chan_FSK.if");
			bw = (uint32_t)json_object_dotget_number(conf_obj, "chan_FSK.bandwidth");
			fdev = (uint32_t)json_object_dotget_number(conf_obj, "chan_FSK.freq_deviation");
			ifconf.datarate = (uint32_t)json_object_dotget_number(conf_obj, "chan_FSK.datarate");

			/* if chan_FSK.bandwidth is set, it has priority over chan_FSK.freq_deviation */
			if ((bw == 0) && (fdev != 0)) {
				bw = 2 * fdev + ifconf.datarate;
			}
			if      (bw == 0)      ifconf.bandwidth = BW_UNDEFINED;
			else if (bw <= 7800)   ifconf.bandwidth = BW_7K8HZ;
			else if (bw <= 15600)  ifconf.bandwidth = BW_15K6HZ;
			else if (bw <= 31200)  ifconf.bandwidth = BW_31K2HZ;
			else if (bw <= 62500)  ifconf.bandwidth = BW_62K5HZ;
			else if (bw <= 125000) ifconf.bandwidth = BW_125KHZ;
			else if (bw <= 250000) ifconf.bandwidth = BW_250KHZ;
			else if (bw <= 500000) ifconf.bandwidth = BW_500KHZ;
			else ifconf.bandwidth = BW_UNDEFINED;

			MSG("INFO: FSK channel> radio %i, IF %i Hz, %u Hz bw, %u bps datarate\n", ifconf.rf_chain, ifconf.freq_hz, bw, ifconf.datarate);
		}
		if (lgw_rxif_setconf(9, ifconf) != LGW_HAL_SUCCESS) {
			MSG("WARNING: invalid configuration for FSK channel\n");
		}
	}
	json_value_free(root_val);
	return 0;
}

static int parse_gateway_configuration(const char * conf_file) {
	const char conf_obj_name[] = "gateway_conf";
	JSON_Value *root_val;
	JSON_Object *conf_obj = NULL;
	JSON_Value *val = NULL; /* needed to detect the absence of some fields */
	const char *str; /* pointer to sub-strings in the JSON data */
	unsigned long long ull = 0;

	/* try to parse JSON */
	root_val = json_parse_file_with_comments(conf_file);
	if (root_val == NULL) {
		MSG("ERROR: %s is not a valid JSON file\n", conf_file);
		exit(EXIT_FAILURE);
	}

	/* point to the gateway configuration object */
	conf_obj = json_object_get_object(json_value_get_object(root_val), conf_obj_name);
	if (conf_obj == NULL) {
		MSG("INFO: %s does not contain a JSON object named %s\n", conf_file, conf_obj_name);
		return -1;
	} else {
		MSG("INFO: %s does contain a JSON object named %s, parsing gateway parameters\n", conf_file, conf_obj_name);
	}

	/* gateway unique identifier (aka MAC address) (optional) */
	str = json_object_get_string(conf_obj, "gateway_ID");
	if (str != NULL) {
		sscanf(str, "%llx", &ull);
		lgwm = ull;
		MSG("INFO: gateway MAC address is configured to %016llX\n", ull);
	}

	/* get interval (in seconds) for statistics display (optional) */
	val = json_object_get_value(conf_obj, "stat_interval");
	if (val != NULL) {
		stat_interval = (unsigned)json_value_get_number(val);
		MSG("INFO: statistics display interval is configured to %u seconds\n", stat_interval);
	}

	/* packet filtering parameters */
	val = json_object_get_value(conf_obj, "forward_crc_valid");
	if (json_value_get_type(val) == JSONBoolean) {
		fwd_valid_pkt = (bool)json_value_get_boolean(val);
	}
	MSG("INFO: packets received with a valid CRC will%s be forwarded\n", (fwd_valid_pkt ? "" : " NOT"));
	val = json_object_get_value(conf_obj, "forward_crc_error");
	if (json_value_get_type(val) == JSONBoolean) {
		fwd_error_pkt = (bool)json_value_get_boolean(val);
	}
	MSG("INFO: packets received with a CRC error will%s be forwarded\n", (fwd_error_pkt ? "" : " NOT"));
	val = json_object_get_value(conf_obj, "forward_crc_disabled");
	if (json_value_get_type(val) == JSONBoolean) {
		fwd_nocrc_pkt = (bool)json_value_get_boolean(val);
	}
	MSG("INFO: packets received with no CRC will%s be forwarded\n", (fwd_nocrc_pkt ? "" : " NOT"));

	/* Auto-quit threshold (optional) */
	val = json_object_get_value(conf_obj, "autoquit_threshold");
	if (val != NULL) {
		autoquit_threshold = (uint32_t)json_value_get_number(val);
		MSG("INFO: Auto-quit after %u non-acknowledged PULL_DATA\n", autoquit_threshold);
	}

	/* free JSON parsing data structure */
	json_value_free(root_val);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* --- MAIN FUNCTION -------------------------------------------------------- */

int main(void)
{
	struct sigaction sigact; /* SIGQUIT&SIGINT&SIGTERM signal handling */
	int i; /* loop variable and temporary variable for return value */

	/* configuration file related */
	char *global_cfg_path= "global_conf.json"; /* contain global (typ. network-wide) configuration */
	char *local_cfg_path = "local_conf.json"; /* contain node specific configuration, overwrite global parameters for parameters that are defined in both */
	char *debug_cfg_path = "debug_conf.json"; /* if present, all other configuration files are ignored */

	//creation de la base de données
	sqlite3 *db;
	char *zErrMsg = 0;
	int  rc;
	char *sql;

	/* Open database */
	FILE *fichier = NULL;
	fichier = fopen("/var/www/bikes.db",  "r");
 	if (fichier == NULL) {
		rc = sqlite3_open("/var/www/bikes.db", &db);
		if( rc ){
			fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
			exit(0);
		}else{
			fprintf(stdout, "Opened database successfully\n");
		}

		/* Create SQL statement */
		sql = 	"CREATE TABLE velos ("  \
			"devaddr	INTEGER	PRIMARY KEY	NOT NULL," \
			"latitude	REAL," \
			"longitude	REAL);" \
			"CREATE TABLE designation (" \
			"velo_id	INTEGER	PRIMARY KEY	NOT NULL," \
			"nom_proprio	VARCHAR(30)," \
			"velo_type	VARCHAR(15));";

		/* Execute SQL statement */
		rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
		if( rc != SQLITE_OK ){
		fprintf(stderr, "SQL error: %s\n", zErrMsg);
			sqlite3_free(zErrMsg);
		}else{
			fprintf(stdout, "Table created successfully\n");
		}
		sqlite3_close(db);
	}
	fclose(fichier);

	/* threads */
	pthread_t thrid_up;

	/* variables to get local copies of measurements */
	uint32_t cp_nb_rx_rcv;
	uint32_t cp_nb_rx_ok;
	uint32_t cp_nb_rx_bad;
	uint32_t cp_nb_rx_nocrc;
	uint32_t cp_up_pkt_fwd;
	uint32_t cp_up_network_byte;
	uint32_t cp_up_payload_byte;
	uint32_t cp_up_dgram_sent;
	uint32_t cp_up_ack_rcv;

	/* statistics variable */
	time_t t;
	char stat_timestamp[24];
	float rx_ok_ratio;
	float rx_bad_ratio;
	float rx_nocrc_ratio;
	float up_ack_ratio;

	/* display version informations */
	MSG("*** Basic Packet Forwarder for Lora Gateway ***\nVersion: " VERSION_STRING "\n");
	MSG("*** Lora concentrator HAL library version info ***\n%s\n***\n", lgw_version_info());

	/* display host endianness */
	#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		MSG("INFO: Little endian host\n");
	#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		MSG("INFO: Big endian host\n");
	#else
		MSG("INFO: Host endianness unknown\n");
	#endif

	/* load configuration files */
	if (access(debug_cfg_path, R_OK) == 0) { /* if there is a debug conf, parse only the debug conf */
		MSG("INFO: found debug configuration file %s, parsing it\n", debug_cfg_path);
		MSG("INFO: other configuration files will be ignored\n");
		parse_SX1301_configuration(debug_cfg_path);
		parse_gateway_configuration(debug_cfg_path);
	} else if (access(global_cfg_path, R_OK) == 0) { /* if there is a global conf, parse it and then try to parse local conf  */
		MSG("INFO: found global configuration file %s, parsing it\n", global_cfg_path);
		parse_SX1301_configuration(global_cfg_path);
		parse_gateway_configuration(global_cfg_path);
		if (access(local_cfg_path, R_OK) == 0) {
			MSG("INFO: found local configuration file %s, parsing it\n", local_cfg_path);
			MSG("INFO: redefined parameters will overwrite global parameters\n");
			parse_SX1301_configuration(local_cfg_path);
			parse_gateway_configuration(local_cfg_path);
		}
	} else if (access(local_cfg_path, R_OK) == 0) { /* if there is only a local conf, parse it and that's all */
		MSG("INFO: found local configuration file %s, parsing it\n", local_cfg_path);
		parse_SX1301_configuration(local_cfg_path);
		parse_gateway_configuration(local_cfg_path);
	} else {
		MSG("ERROR: [main] failed to find any configuration file named %s, %s OR %s\n", global_cfg_path, local_cfg_path, debug_cfg_path);
		exit(EXIT_FAILURE);
	}

	/* sanity check on configuration variables */
	// TODO

	/* starting the concentrator */
	i = lgw_start();
	if (i == LGW_HAL_SUCCESS) {
		MSG("INFO: [main] concentrator started, packet can now be received\n");
	} else {
		MSG("ERROR: [main] failed to start the concentrator\n");
		exit(EXIT_FAILURE);
	}

	/* spawn threads to manage upstream and downstream */
	i = pthread_create( &thrid_up, NULL, (void * (*)(void *))thread_up, NULL);
	if (i != 0) {
		MSG("ERROR: [main] impossible to create upstream thread\n");
		exit(EXIT_FAILURE);
	}

	/* configure signal handling */
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = sig_handler;
	sigaction(SIGQUIT, &sigact, NULL); /* Ctrl-\ */
	sigaction(SIGINT, &sigact, NULL); /* Ctrl-C */
	sigaction(SIGTERM, &sigact, NULL); /* default "kill" command */

	/* main loop task : statistics collection */
	while (!exit_sig && !quit_sig) {
		/* wait for next reporting interval */
		wait_ms(1000 * stat_interval);

		/* get timestamp for statistics */
		t = time(NULL);
		strftime(stat_timestamp, sizeof stat_timestamp, "%F %T %Z", gmtime(&t));

		/* access upstream statistics, copy and reset them */
		pthread_mutex_lock(&mx_meas_up);
		cp_nb_rx_rcv       = meas_nb_rx_rcv;
		cp_nb_rx_ok        = meas_nb_rx_ok;
		cp_nb_rx_bad       = meas_nb_rx_bad;
		cp_nb_rx_nocrc     = meas_nb_rx_nocrc;
		cp_up_pkt_fwd      = meas_up_pkt_fwd;
		cp_up_network_byte = meas_up_network_byte;
		cp_up_payload_byte = meas_up_payload_byte;
		cp_up_dgram_sent   = meas_up_dgram_sent;
		cp_up_ack_rcv      = meas_up_ack_rcv;
		meas_nb_rx_rcv = 0;
		meas_nb_rx_ok = 0;
		meas_nb_rx_bad = 0;
		meas_nb_rx_nocrc = 0;
		meas_up_pkt_fwd = 0;
		meas_up_network_byte = 0;
		meas_up_payload_byte = 0;
		meas_up_dgram_sent = 0;
		meas_up_ack_rcv = 0;
		pthread_mutex_unlock(&mx_meas_up);
		if (cp_nb_rx_rcv > 0) {
			rx_ok_ratio = (float)cp_nb_rx_ok / (float)cp_nb_rx_rcv;
			rx_bad_ratio = (float)cp_nb_rx_bad / (float)cp_nb_rx_rcv;
			rx_nocrc_ratio = (float)cp_nb_rx_nocrc / (float)cp_nb_rx_rcv;
		} else {
			rx_ok_ratio = 0.0;
			rx_bad_ratio = 0.0;
			rx_nocrc_ratio = 0.0;
		}
		if (cp_up_dgram_sent > 0) {
			up_ack_ratio = (float)cp_up_ack_rcv / (float)cp_up_dgram_sent;
		} else {
			up_ack_ratio = 0.0;
		}

		/* display a report */
		printf("\n##### %s #####\n", stat_timestamp);
		printf("### [UPSTREAM] ###\n");
		printf("# RF packets received by concentrator: %u\n", cp_nb_rx_rcv);
		printf("# CRC_OK: %.2f%%, CRC_FAIL: %.2f%%, NO_CRC: %.2f%%\n", 100.0 * rx_ok_ratio, 100.0 * rx_bad_ratio, 100.0 * rx_nocrc_ratio);
		printf("# RF packets forwarded: %u (%u bytes)\n", cp_up_pkt_fwd, cp_up_payload_byte);
		printf("# PUSH_DATA datagrams sent: %u (%u bytes)\n", cp_up_dgram_sent, cp_up_network_byte);
		printf("# PUSH_DATA acknowledged: %.2f%%\n", 100.0 * up_ack_ratio);
		printf("##### END #####\n");
	}

	/* wait for upstream thread to finish (1 fetch cycle max) */
	pthread_join(thrid_up, NULL);

	/* if an exit signal was received, try to quit properly */
	if (exit_sig) {
		/* stop the hardware */
		i = lgw_stop();
		if (i == LGW_HAL_SUCCESS) {
			MSG("INFO: concentrator stopped successfully\n");
		} else {
			MSG("WARNING: failed to stop concentrator successfully\n");
		}
	}

	MSG("INFO: Exiting packet forwarder program\n");
	exit(EXIT_SUCCESS);
}

/* -------------------------------------------------------------------------- */
/* --- THREAD 1: RECEIVING PACKETS AND FORWARDING THEM ---------------------- */

void thread_up(void) {
	int i; /* loop variables */

	/* allocate memory for packet fetching and processing */
	struct lgw_pkt_rx_s rxpkt[NB_PKT_MAX]; /* array containing inbound packets + metadata */
	struct lgw_pkt_rx_s *p; /* pointer on a RX packet */
	int nb_pkt;

	/* local timestamp variables until we get accurate GPS time */
	struct timespec fetch_time;
	struct tm * x;
	char fetch_timestamp[28]; /* timestamp as a text string */

	while (!exit_sig && !quit_sig) {

		/* fetch packets */
		pthread_mutex_lock(&mx_concent);
		nb_pkt = lgw_receive(NB_PKT_MAX, rxpkt);
		pthread_mutex_unlock(&mx_concent);
		if (nb_pkt == LGW_HAL_ERROR) {
			MSG("ERROR: [up] failed packet fetch, exiting\n");
			exit(EXIT_FAILURE);
		} else if (nb_pkt == 0) {
			wait_ms(FETCH_SLEEP_MS); /* wait a short time if no packets */
			continue;
		}

		/* local timestamp generation until we get accurate GPS time */
		clock_gettime(CLOCK_REALTIME, &fetch_time);
		x = gmtime(&(fetch_time.tv_sec)); /* split the UNIX timestamp to its calendar components */
		snprintf(fetch_timestamp, sizeof fetch_timestamp, "%04i-%02i-%02iT%02i:%02i:%02i.%06liZ", (x->tm_year)+1900, (x->tm_mon)+1, x->tm_mday, x->tm_hour, x->tm_min, x->tm_sec, (fetch_time.tv_nsec)/1000); /* ISO 8601 format */

		/* serialize Lora packets metadata and payload */
		for (i=0; i < nb_pkt; ++i) {
			p = &rxpkt[i];

			/* basic packet filtering */
			pthread_mutex_lock(&mx_meas_up);
			meas_nb_rx_rcv += 1;
			switch(p->status) {
				case STAT_CRC_OK:
					meas_nb_rx_ok += 1;
					if (!fwd_valid_pkt) {
						pthread_mutex_unlock(&mx_meas_up);
						continue; /* skip that packet */
					}
					break;
				case STAT_CRC_BAD:
					meas_nb_rx_bad += 1;
					if (!fwd_error_pkt) {
						pthread_mutex_unlock(&mx_meas_up);
						continue; /* skip that packet */
					}
					break;
				case STAT_NO_CRC:
					meas_nb_rx_nocrc += 1;
					if (!fwd_nocrc_pkt) {
						pthread_mutex_unlock(&mx_meas_up);
						continue; /* skip that packet */
					}
					break;
				default:
					MSG("WARNING: [up] received packet with unknown status %u (size %u, modulation %u, BW %u, DR %u, RSSI %.1f)\n", p->status, p->size, p->modulation, p->bandwidth, p->datarate, p->rssi);
					pthread_mutex_unlock(&mx_meas_up);
					continue; /* skip that packet */
					// exit(EXIT_FAILURE);
			}
			meas_up_pkt_fwd += 1;
			meas_up_payload_byte += p->size;
			pthread_mutex_unlock(&mx_meas_up);

			//affichage du contenu du paquet
			int j = 0;
			printf("\npayload :\n");
			for(i=0; i<(p->size); i++) {
				printf("%02x ", p->payload[i]);
				j++;
				if(j == 4) {
					printf("\n");
					j = 0;
				}
			}

			//recuperation de la devaddr
			printf("\ndevaddr: %02x %02x %02x %02x\n", p->payload[4], p->payload[3], p->payload[2], p->payload[1]);
			uint32_t devaddr = ((uint32_t)p->payload[4] << 24) | ((uint32_t)p->payload[3] << 16) | ((uint32_t)p->payload[2] << 8) | p->payload[1];

			//recuperation du sequencecounter
			uint16_t FCnt = ((uint16_t)p->payload[7] << 8) | p->payload[6];
			printf("FCnt: %d\n", FCnt);

			//recuperation de la data
			int FOpts_len = ( p->payload[5] ) & 0x0F;
			printf("FOpts_len: %d\n", FOpts_len);
			uint16_t datasize = (uint16_t)((p->size) - (13 + FOpts_len));
			const uint8_t* data = &(p->payload[9 + FOpts_len]);
                        printf("data: ");
			for(i=0; i<datasize; i++) {
				printf("%02x ", data[i]);
			}
			printf("\n");
			if(datasize == 8) {
				//decryption de la payload
				uint8_t decdata[datasize];
				uint8_t up = 0;
				LoRaMacPayloadDecrypt(data, datasize, LoRaMacAppSKey, devaddr, up, (uint32_t)FCnt, decdata );
				printf("decdata: ");
				for(i=0; i<datasize; i++) {
					printf("%02x ", decdata[i]);
				}
				printf(" \n");

				//ecriture de la payload dans le fichier de log
				FILE * fp;
				fp = fopen ("log_coordinates.txt", "r+");
				char command[50];
				//aller à la derniere ligne du fichier
				if(fp != NULL) {
					fseek(fp, 0, SEEK_END);
					//puis on imprime la payload
					for(i=0;i<datasize;i++) {
						fprintf(fp, "%02x", decdata[i]);
					}
					fprintf(fp, "\n");
					system(command);
					fclose(fp);
				}
				else {
					printf("fichier corrompu");
				}
				//appel du programme de conversion de type
				strcpy(command, "python coord.py");
				system(command);
				//wait le temps que le python fasse le boulot
				sleep(1);
				//recuperation de la derniere ligne et formatage dans lat et long
                                fp = fopen ("log_coordinates.txt", "r+");
				char line[1024];
				char new_line[1024];
				float latitude;
				float longitude;
				if( fp ) {
      					while( fgets(line, 1024, fp) !=NULL ) {
      						strcpy(new_line, line);
      					}
      					printf("Last line %s\n", new_line);
      					sscanf(new_line, "%f-%f", &latitude, &longitude);
					fclose(fp);
					printf("latitude: %f; longitude: %f\n", latitude, longitude);
				}
				else {
					printf("fichier corrompu 2\n");
				}
				/*
				union u latitude;
                                for(i=0;i<4;i++) {
                                        latitude.b[i] = decdata[i];
                                        printf("%02x ", latitude.b[i]);
                                }
                                printf("\n");

                                printf("\nlatitude: %f\n", latitude.f);
				union u longitude;
                                longitude.f = 11.11;
                                printf("\n");
				*/
				/*
				float latitude = 48.581485;
				float longitude = 7.744031;
				char temp_buff[datasize];
				sprintf(temp_buff, "%f%f", latitude, longitude);
				printf("lat et long dans le temp: ");
				for(i=0; i<datasize; i++) {
                                        printf("%02x ", temp_buff[i]);
                                }
				printf(" \n");
				*/
				/*
				latitude = 0;
				longitude = 0;
				const char* temp_buff2 = decdata;
				sscanf(temp_buff2, "%f%f", &latitude, &longitude);
				printf("latitude: %f, longitude: %f\n", latitude, longitude);
				*/
				//recuperation de la latitude et de la longitude
				/*
				float latitude = (float)((decdata[0] << 24) | (decdata[1] << 16) | (decdata[2] << 8) | decdata[3]);
				printf("latitude: %f\n", latitude);
				float longitude;
				*/
				/*
				memcpy(&latitude, decdata, sizeof latitude);
				memcpy(&longitude, &decdata[datasize/2],  sizeof longitude);
				*/
				/*
				for(i=0; i<(datasize/2); i++) {
					memcpy((&latitude + i), &(decdata[i]), sizeof(uint8_t));
				}
				for(i=(datasize/2); i<datasize; i++) {
                                        memcpy((&longitude + i - (datasize/2)), &(decdata[i]), sizeof(uint8_t));
                                }
				*/
				/*
				union u latitude;
				for(i=0;i<4;i++) {
					latitude.b[i] = decdata[i];
					printf("%02x ", latitude.b[i]);
				}
				printf("\n");
				union u longitude;
                                for(i=4;i<8;i++) {
                                        longitude.b[i-4] = decdata[i];
					printf("%02x ", longitude.b[i-4]);
                                }
				printf("\nlatitude: %08x\n", latitude.f);
				printf("longitude: %08x\n", longitude.f);
				*/

				//ecriture dans la data base
				sqlite3 *db;
	   			char *zErrMsg = 0;
				int  rc;
				const char* sql_data = "Callback function called";
				char *select = "SELECT * FROM velos";
				/* Open database */
				rc = sqlite3_open("/var/www/bikes.db", &db);
				if( rc ){
					fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
					exit(-1);
				}
				else{
					fprintf(stdout, "Opened database successfully\n");
				}

				/* Create SQL statement */
				char sql[1000];
				char temp[200];
				strcpy(sql, "INSERT INTO velos (devaddr,latitude,longitude) VALUES (");
				sprintf(temp, "%d",devaddr);
				strcat(sql, temp);
				strcat(sql, ", ");
				sprintf(temp, "%f",latitude);
				strcat(sql, temp);
	                        strcat(sql, ", ");
				sprintf(temp, "%f",longitude);
	                        strcat(sql, temp);
				/*
				for(i=0; i<datasize; i++) {
					sprintf(temp, "%d",decdata[i]);
					strcat(sql, temp);
				}
				*/
				strcat(sql, ");");
				printf("%s\n", sql);

				/* Execute SQL statement */
				rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
				printf("%d\n", rc);
				if( rc == SQLITE_CONSTRAINT ) {
					fprintf(stderr, "SQL error: %s\n", zErrMsg);
					sqlite3_free(zErrMsg);
					//update
					/* Create SQL statement */
	                        	strcpy(sql, "UPDATE velos SET latitude = ");
					/*
					for(i=0; i<datasize; i++) {
	                                        sprintf(temp, "%d",decdata[i]);
	                                        strcat(sql, temp);
	                                }
					*/
					sprintf(temp, "%f",latitude);
					strcat(sql, temp);
					strcat(sql, ", longitude = ");
					sprintf(temp, "%f",longitude);
	                                strcat(sql, temp);
					strcat(sql, " WHERE DEVADDR = ");
					sprintf(temp, "%d",devaddr);
	                        	strcat(sql, temp);
					strcat(sql, ";");
	                        	printf("%s\n", sql);

	                        	/* Execute SQL statement */
	                        	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
					printf("%d\n", rc);
					if( rc != SQLITE_OK ){
						fprintf(stderr, "SQL error: %s\n", zErrMsg);
						sqlite3_free(zErrMsg);
					}
					else {
						fprintf(stdout, "successfully update\n");
					}
				}
				else if(rc == SQLITE_OK) {
					fprintf(stdout, "successfully added\n");
				}
				else {
					fprintf(stderr, "SQL error: %s\n", zErrMsg);
	                                sqlite3_free(zErrMsg);
				}

				/* Execute SELECT statement */
				rc = sqlite3_exec(db, select, callback, (void*)sql_data, &zErrMsg);
				if( rc != SQLITE_OK ){
					fprintf(stderr, "SQL error: %s\n", zErrMsg);
					sqlite3_free(zErrMsg);
				}
				else {
					fprintf(stdout, "Operation done successfully\n");
				}
				sqlite3_close(db);
			}
			else {
				printf("data au mauvais format\n");
			}
			printf("\n");
		}

		pthread_mutex_unlock(&mx_meas_up);
	}
	MSG("\nINFO: End of upstream thread\n");
}

/* --- EOF ------------------------------------------------------------------ */
