/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    �2013 Semtech-Cycleo

Description:
	Send a bunch of packets
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
#include <stdio.h>		/* printf fprintf sprintf fopen fputs */

#include <string.h>		/* memset */
#include <signal.h>		/* sigaction */
#include <unistd.h>		/* getopt access */
#include <stdlib.h>		/* atoi */

#include "loragw_hal.h"
#include "loragw_aux.h"

/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))
#define MSG(args...)	fprintf(stderr,"loragw_pkt_logger: " args) /* message that is destined to the user */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE CONSTANTS ---------------------------------------------------- */

#define		RF_CHAIN				0	/* we'll use radio A only */

const uint32_t lowfreq[LGW_RF_CHAIN_NB] = LGW_RF_TX_LOWFREQ;
const uint32_t upfreq[LGW_RF_CHAIN_NB] = LGW_RF_TX_UPFREQ;

/* -------------------------------------------------------------------------- */
/* --- PRIVATE VARIABLES (GLOBAL) ------------------------------------------- */

/* signal handling variables */
struct sigaction sigact; /* SIGQUIT&SIGINT&SIGTERM signal handling */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE FUNCTIONS DECLARATION ---------------------------------------- */

static void sig_handler(int sigio);

/* -------------------------------------------------------------------------- */
/* --- PRIVATE FUNCTIONS DEFINITION ----------------------------------------- */

static void sig_handler(int sigio) {
	if ((sigio == SIGQUIT) || (sigio == SIGINT) || (sigio == SIGTERM)) {
		exit(EXIT_FAILURE);
	}
}

/* -------------------------------------------------------------------------- */
/* --- MAIN FUNCTION -------------------------------------------------------- */

int main(int argc, char **argv)
{
	int i;
	uint8_t status_var;
	
	/* user entry parameters */
	int xi = 0;
	double xd = 0.0;
	uint32_t f_min;
	uint32_t f_max;
	
	/* application parameters */
	uint32_t f_target = lowfreq[RF_CHAIN]/2 + upfreq[RF_CHAIN]/2; /* target frequency */
	int sf = 10; /* SF10 by default */
	int bw = 125; /* 125kHz bandwidth by default */
	int pow = 14; /* 14 dBm by default */
	int delay = 1000; /* 1 second between packets by default */
	int repeat = 1; /* sweep only once by default */
	
	/* RF configuration (TX fail if RF chain is not enabled) */
	const struct lgw_conf_rxrf_s rfconf = {true, lowfreq[RF_CHAIN]};
	
	/* allocate memory for packet sending */
	struct lgw_pkt_tx_s txpkt; /* array containing 1 outbound packet + metadata */
	
	/* loop variables (also use as counters in the packet payload) */
	uint16_t cycle_count = 0;
	
	/* parse command line options */
	while ((i = getopt (argc, argv, "hf:s:b:p:t:x:")) != -1) { /* process bandwidth first */
		switch (i) {
			case 'h':
				MSG( "Available options:\n");
				MSG( "-h print this help\n");
				MSG( "-f target frequency in MHz\n");
				MSG( "-s Spreading Factor\n");
				MSG( "-b Modulation bandwidth in kHz\n");
				MSG( "-p RF power (dBm)\n");
				MSG( "-t pause between packets (ms)\n");
				MSG( "-x numbers of times the sequence is repeated\n");
				return EXIT_FAILURE;
				break;
			
			case 'f':
				i = sscanf(optarg, "%lf", &xd);
				if ((i != 1) || (xd < 30.0) || (xd > 3000.0)) {
					MSG("ERROR: invalid TX frequency\n");
					return EXIT_FAILURE;
				} else {
					f_target = (uint32_t)((xd*1e6) + 0.5); /* .5 Hz offset to get rounding instead of truncating */
				}
				break;
			
			case 's':
				i = sscanf(optarg, "%i", &xi);
				if ((i != 1) || (xi < 7) || (xi > 12)) {
					MSG("ERROR: invalid spreading factor\n");
					return EXIT_FAILURE;
				} else {
					sf = xi;
				}
				break;
			
			case 'b':
				i = sscanf(optarg, "%i", &xi);
				if ((i != 1) || ((xi != 125)&&(xi != 250)&&(xi != 500))) {
					MSG("ERROR: invalid Lora bandwidth\n");
					return EXIT_FAILURE;
				} else {
					bw = xi;
				}
				break;
			
			case 'p':
				i = sscanf(optarg, "%i", &xi);
				if ((i != 1) || (xi < 0) || (xi > 20)) {
					MSG("ERROR: invalid RF power\n");
					return EXIT_FAILURE;
				} else {
					pow = xi;
				}
				break;
			
			case 't':
				i = sscanf(optarg, "%i", &xi);
				if ((i != 1) || (xi < 0)) {
					MSG("ERROR: invalid time between packets\n");
					return EXIT_FAILURE;
				} else {
					delay = xi;
				}
				break;
			
			case 'x':
				i = sscanf(optarg, "%i", &xi);
				if ((i != 1) || (xi < 1)) {
					MSG("ERROR: invalid number of repeats\n");
					return EXIT_FAILURE;
				} else {
					repeat = xi;
				}
				break;
			
			default:
				MSG("ERROR: argument parsing use -h option for help\n");
				return EXIT_FAILURE;
		}
	}
	
	/* configure signal handling */
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = sig_handler;
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	
	/* check parameter sanity */
	f_min = lowfreq[RF_CHAIN] + (500 * bw);
	f_max = upfreq[RF_CHAIN] - (500 * bw);
	if ((f_target < f_min) || (f_target > f_max)) {
		MSG("ERROR: frequency out of authorized band (accounting for modulation bandwidth)\n");
		return EXIT_FAILURE;
	}
	
	/* starting the gateway */
	lgw_rxrf_setconf(RF_CHAIN, rfconf);
	i = lgw_start();
	if (i == LGW_HAL_SUCCESS) {
		MSG("INFO: gateway started, packet can be sent\n");
	} else {
		MSG("ERROR: failed to start the gateway\n");
		return EXIT_FAILURE;
	}
	
	/* fill-up payload and parameters */
	memset(&txpkt, 0, sizeof(txpkt));
	txpkt.freq_hz = f_target;
	txpkt.tx_mode = IMMEDIATE;
	txpkt.rf_chain = RF_CHAIN;
	txpkt.rf_power = pow;
	txpkt.modulation = MOD_LORA;
	switch (bw) {
		case 125: txpkt.bandwidth = BW_125KHZ; break;
		case 250: txpkt.bandwidth = BW_250KHZ; break;
		case 500: txpkt.bandwidth = BW_500KHZ; break;
		default:
			MSG("ERROR: invalid 'bw' variable\n");
			return EXIT_FAILURE;
	}
	switch (sf) {
		case  7: txpkt.datarate = DR_LORA_SF7;  break;
		case  8: txpkt.datarate = DR_LORA_SF8;  break;
		case  9: txpkt.datarate = DR_LORA_SF9;  break;
		case 10: txpkt.datarate = DR_LORA_SF10; break;
		case 11: txpkt.datarate = DR_LORA_SF11; break;
		case 12: txpkt.datarate = DR_LORA_SF12; break;
		default:
			MSG("ERROR: invalid 'sf' variable\n");
			return EXIT_FAILURE;
	}
	txpkt.coderate = CR_LORA_4_5;
	// txpkt.invert_pol // TODO
	txpkt.preamble = 8;
	txpkt.size = 20; /* should be close to typical payload length */
	strcpy((char *)txpkt.payload, "TEST**abcdefghijklmn" ); /* abc.. is for padding */
	
	MSG("INFO: Sending %u packets on %u Hz (BW %u kHz, SF %u, 20 bytes payload) at %i dBm, with %u ms between each.\n", repeat, f_target, bw, sf, pow, delay);
	
	/* main loop */
	for (cycle_count = 0; cycle_count < repeat; ++cycle_count) {
		/* refresh counters in payload (big endian, for readability) */
		txpkt.payload[4] = (uint8_t)(cycle_count >> 8); /* MSB */
		txpkt.payload[5] = (uint8_t)(cycle_count & 0x00FF); /* LSB */
		
		/* send packet */
		printf("Sending packet number %u ...", cycle_count);
		i = lgw_send(txpkt); /* non-blocking scheduling of TX packet */
		do {
			wait_ms(5);
			lgw_status(TX_STATUS, &status_var); /* get TX status */
		} while (status_var != TX_FREE);
		printf("OK\n");
		
		/* wait inter-packet delay */
		wait_ms(delay);
	}
	
	/* clean up before leaving */
	lgw_stop();
	
	MSG("INFO: Exiting TX test program\n");
	return EXIT_SUCCESS;
}

/* --- EOF ------------------------------------------------------------------ */
