/*
   simple demonstration daemon for the MLX90621 16x4 thermopile array

   Modified by Todd Erickson for the MLX90621

   Forked from https://github.com/alphacharlie/mlxd written by Chuck Werbick

   Copyright (C) 2015 Chuck Werbick

   Based upon the program 'piir' by Mike Strean

   Copyright (C) 2013 Mike Strean

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <bcm2835.h>

#define VERSION "0.1.0"
#define EXIT_FAILURE 1

char *xmalloc ();
char *xrealloc ();
char *xstrdup ();

float temperatures[64];
unsigned short temperaturesInt[64];
static int usage (int status);

/* The name the program was run with, stripped of any leading path. */
char *program_name;

/* getopt_long return codes */
enum {DUMMY_CODE=129
};

/* Option flags and variables */


static struct option const long_options[] =
{
  {"help", no_argument, 0, 'h'},
  {"version", no_argument, 0, 'V'},
  {NULL, 0, NULL, 0}
};

static int decode_switches (int argc, char **argv);
int mlx90621_init ();
int mlx90621_read_eeprom ();
int mlx90621_write_config (unsigned char *lsb, unsigned char *msb);
int mlx90621_read_config (unsigned char *lsb, unsigned char *msb);
int mlx90621_write_trim (char t);
char mlx90621_read_trim ();
int mlx90621_por ();
int mlx90621_set_refresh_hz (int hz);
int mlx90621_ptat ();
int mlx90621_cp ();
float mlx90621_ta ();
int mlx90621_ir_read ();
void calc_to(float ta,  int vcp);


char EEPROM[256];
signed char ir_pixels[128];
int irData[64];

char mlxFifo[] = "/var/run/mlx90621.sock";


void got_sigint(int sig) {
    unlink(mlxFifo);
    bcm2835_i2c_end();
    exit(0);

}


main (int argc, char **argv)
{
    signal(SIGINT, got_sigint);
    int fd;

    mkfifo(mlxFifo, 0666);

    int x;
    int i, j;

    float to;
    float ta;
    int vir;
    int vcp;
    float alpha;
    float vir_compensated;
    float vcp_off_comp, vir_off_comp, vir_tgc_comp;

    /* IR pixel individual offset coefficient */
    int ai;
    /* Individual Ta dependence (slope) of IR pixels offset */
    int bi;
    /* Individual sensitivity coefficient */
    int delta_alpha;
    /* Compensation pixel individual offset coefficients */
    int acp;
    /* Individual Ta dependence (slope) of the compensation pixel offset */
    int bcp;
    /* Sensitivity coefficient of the compensation pixel */
    int alphacp;
    /* Thermal Gradient Coefficient */
    int tgc;
    /* Scaling coefficient for slope of IR pixels offset */
    int bi_scale;
    /* Common sensitivity coefficient of IR pixels */
    int alpha0;
    /* Scaling coefficient for common sensitivity */
    int alpha0_scale;
    /* Scaling coefficient for individual sensitivity */
    int delta_alpha_scale;
    /* Emissivity */
    float epsilon;
    int retryCount, mlxReadVal;

    retryCount = 0;
    program_name = argv[0];

    i = decode_switches (argc, argv);


    printf("\n");

    if ( mlx90621_init() ) {
        printf("OK, MLX90621 init\n");
    } else {
        printf("MLX90621 init failed!\n");
        exit(1);
    }
    ta = mlx90621_ta();
    printf("Ta reading: %4.8f C\n", ta);
    // If calibration fails then TA will be WAY too high. check and reinitialize if that happens
    while ((ta > 350 || ta != ta) && retryCount < 2)
    {
    	printf("Ta out of bounds! Max is 350, reading: %4.8f C\n", ta);
    	//out of bounds, reset and check again
    	mlx90621_init();
    	ta = mlx90621_ta();
    	usleep(10000);
	retryCount++;
    }

    printf("Ta = %4.8f C %4.8f F\n\n", ta, ta * (9.0/5.0) + 32.0);

    /* To calc parameters */
    vcp = mlx90621_cp();
    acp = (signed char)EEPROM[0xD4];
    bcp = (signed char)EEPROM[0xD5];
    alphacp = ( EEPROM[0xD7] << 8 ) | EEPROM[0xD6];
    tgc = (signed char)EEPROM[0xD8];
    bi_scale = EEPROM[0xD9];
    alpha0 = ( EEPROM[0xE1] << 8 ) | EEPROM[0xE0];
    alpha0_scale = EEPROM[0xE2];
    delta_alpha_scale = EEPROM[0xE3];
    epsilon = (( EEPROM[0xE5] << 8 ) | EEPROM[0xE4] ) / 32768.0;


    /* do the work */

    do {

        /* POR/Brown Out flag */

        while (!mlx90621_por) {
            sleep(1);
            mlx90621_init();
        }

        mlxReadVal = mlx90621_ir_read();

        if ( !mlxReadVal ){
            printf("Could not read IR values \n");
            exit(0);
        }

        /* Calculate To */
        calc_to(ta, vcp);

        fd = open(mlxFifo, O_WRONLY);
        write(fd, temperaturesInt, sizeof(temperaturesInt));
        close(fd);
        usleep(100000);
    } while (1);

    unlink(mlxFifo);

    exit (0);
}


/* Init */

int
mlx90621_init()
{
    if (!bcm2835_init()) return 0;
    bcm2835_i2c_begin();
    bcm2835_i2c_set_baudrate(25000);

    //sleep 5ms per datasheet
    usleep(5000);
    if ( !mlx90621_read_eeprom() ) return 0;
    if ( !mlx90621_write_trim( EEPROM[0xF7] ) ) return 0;
    if ( !mlx90621_write_config( &EEPROM[0xF5], &EEPROM[0xF6] ) ) return 0;

    mlx90621_set_refresh_hz( 4 );

    unsigned char lsb, msb;
    mlx90621_read_config( &lsb, &msb );

    return 1;
}

/* Read the whole EEPROM */

int
mlx90621_read_eeprom()
{
    const unsigned char read_eeprom[] = {
        0x00 // command
    };

    bcm2835_i2c_begin();
    bcm2835_i2c_setSlaveAddress(0x50);
    if (
        bcm2835_i2c_write_read_rs((char *)&read_eeprom, 1, EEPROM, 256)
        == BCM2835_I2C_REASON_OK
        ) return 1;

    return 0;
}

/* Write device configuration value */

int
mlx90621_write_config(unsigned char *lsb, unsigned char *msb)
{
    unsigned char lsb_check = lsb[0] - 0x55;
    unsigned char msb_check = msb[0] - 0x55;

    unsigned char write_config[] = {
        0x03, // command
        lsb_check,
        lsb[0],
        msb_check,
        msb[0]
    };

    bcm2835_i2c_begin();
    bcm2835_i2c_setSlaveAddress(0x60);
    if (
        bcm2835_i2c_write((const char *)&write_config, 5)
        == BCM2835_I2C_REASON_OK
        ) return 1;

    return 0;
}

/* Reading configuration */

int
mlx90621_read_config(unsigned char *lsb, unsigned char *msb)
{
    unsigned char config[2];

    const unsigned char read_config[] = {
        0x02, // command
        0x92, // start address
        0x00, // address step
        0x01  // number of reads
    };

    bcm2835_i2c_begin();
    bcm2835_i2c_setSlaveAddress(0x60);
    if (
        !bcm2835_i2c_write_read_rs((char *)&read_config, 4, config, 2)
        == BCM2835_I2C_REASON_OK
        ) return 0;

    *lsb = config[0];
    *msb = config[1];
    return 1;
}

/* Write the oscillator trimming value */

int
mlx90621_write_trim(char t)
{
    unsigned char trim[] = {
        0x00, // MSB
        t     // LSB
    };
    unsigned char trim_check_lsb = trim[1] - 0xAA;
    unsigned char trim_check_msb = trim[0] - 0xAA;

    unsigned char write_trim[] = {
        0x04, // command
        trim_check_lsb,
        trim[1],
        trim_check_msb,
        trim[0]
    };

    bcm2835_i2c_begin();
    bcm2835_i2c_setSlaveAddress(0x60);
    if (
        bcm2835_i2c_write((char *)&write_trim, 5)
        == BCM2835_I2C_REASON_OK
        ) return 1;

    return 0;
}

/* Read oscillator trimming register */

char
mlx90621_read_trim()
{
    unsigned char trim_bytes[2];

    const unsigned char read_trim[] = {
        0x02, // command
        0x93, // start address
        0x00, // address step
        0x01  // number of reads
    };

    bcm2835_i2c_begin();
    bcm2835_i2c_setSlaveAddress(0x60);
    if (
        bcm2835_i2c_write_read_rs((char *)&read_trim, 4, trim_bytes, 2)
        == BCM2835_I2C_REASON_OK
        ) return 1;

    return trim_bytes[0];
}

/* Return POR/Brown-out flag */

int
mlx90621_por()
{
    unsigned char config_lsb, config_msb;

    mlx90621_read_config( &config_lsb, &config_msb );
    return ((config_msb & 0x04) == 0x04);
}

/* Set IR Refresh rate */

int
mlx90621_set_refresh_hz(int hz)
{
    char rate_bits;

    switch (hz) {
        case 512:
            rate_bits = 0b0000;
            break;
        case 256:
            rate_bits = 0b0110;
            break;
        case 128:
            rate_bits = 0b0111;
            break;
        case 64:
            rate_bits = 0b1000;
            break;
        case 32:
            rate_bits = 0b1001;
            break;
        case 16:
            rate_bits = 0b1010;
            break;
        case 8:
            rate_bits = 0b1011;
            break;
        case 4:
            rate_bits = 0b1100;
            break;
        case 2:
            rate_bits = 0b1101;
            break;
        case 1:
            rate_bits = 0b1110; // default
            break;
        case 0:
            rate_bits = 0b1111; // 0.5 Hz
            break;
        default:
            rate_bits = 0b1110;
    }

    unsigned char config_lsb, config_msb;
    if ( !mlx90621_read_config( &config_lsb, &config_msb ) ) return 0;
    config_lsb = rate_bits;
    if ( !mlx90621_write_config( &config_lsb, &config_msb ) ) return 0;

    return 1;
}

/* Return PTAT (Proportional To Absolute Temperature) */

int
mlx90621_ptat()
{
    int ptat;
    unsigned char ptat_bytes[2];

    const unsigned char read_ptat[] = {
        0x02, // command
        0x40, // start address
        0x00, // address step
        0x01  // number of reads
    };

    bcm2835_i2c_begin();
    bcm2835_i2c_setSlaveAddress(0x60);
    if (
        !bcm2835_i2c_write_read_rs((char *)&read_ptat, 4, (char *)&ptat_bytes, 2)
        == BCM2835_I2C_REASON_OK
        ) return 0;

    ptat = ( ptat_bytes[1] << 8 ) | ptat_bytes[0];
    return ptat;
}

/* Compensation pixel read */

int
mlx90621_cp()
{
    int cp;
    signed char VCP_BYTES[2];

    const unsigned char compensation_pixel_read[] = {
        0x02, // command
        0x41, // start address
        0x00, // address step
        0x01  // number of reads
    };

    bcm2835_i2c_begin();
    bcm2835_i2c_setSlaveAddress(0x60);
    if (
        !bcm2835_i2c_write_read_rs((char *)&compensation_pixel_read, 4, (char *)&VCP_BYTES, 2)
        == BCM2835_I2C_REASON_OK
        ) return 0;

    cp = ( VCP_BYTES[1] << 8 ) | VCP_BYTES[0];
    if (cp >= 32768)
        cp -= 65536;
    return cp;
}

/* calculation of absolute chip temperature */

float
mlx90621_ta()
{
	int ptat = mlx90621_ptat();
	char KT_SCALE = 0xD2;
    char VTH_H = 0xDB;
    char VTH_L = 0xDA;
    char KT1_H = 0xDD;
	char KT1_L = 0xDC;
    char KT2_H = 0xDF;
    char KT2_L = 0xDE;
	unsigned char lsb, msb;
	int resolution;
	int k_t1_scale = (int) (EEPROM[KT_SCALE] & 0xF0) >> 4;
	int k_t2_scale = (int) (EEPROM[KT_SCALE] & 0x0F) + 10;
	float v_th = (float) 256 * EEPROM[VTH_H] + EEPROM[VTH_L];
	float k_t1 = (float) 256 * EEPROM[KT1_H] + EEPROM[KT1_L];
    float k_t2 = (float) 256 * EEPROM[KT2_H] + EEPROM[KT2_L];
	mlx90621_read_config(&lsb, &msb);
    resolution = (((int) (msb << 8) | lsb) & 0x30) >> 4;

	if (v_th >= 32768.0)
		v_th -= 65536.0;
	v_th = v_th / pow((3 - resolution), 2);

	if (k_t1 >= 32768.0)
		k_t1 -= 65536.0;
	k_t1 /= (pow(k_t1_scale, 2) * pow((3 - resolution), 2));

	if (k_t2 >= 32768.0)
		k_t2 -= 65536.0;
	k_t2 /= (pow(k_t2_scale, 2) * pow((3 - resolution), 2));

	return ((-k_t1 + sqrt(pow(k_t1, 2) - (4 * k_t2 * (v_th - (float) ptat))))
			/ (2 * k_t2)) + 25.0;

}

void
calc_to(float ta,  int vcp)
{
    char CAL_EMIS_H = 0xE5;
    char CAL_EMIS_L = 0xE4;
    char CAL_ACOMMON_H = 0xD1;
    char CAL_ACOMMON_L = 0xD0;
    char CAL_alphaCP_H = 0xD7;
    char CAL_alphaCP_L = 0xD6;
    int CAL_A0_SCALE = 226;
    char CAL_AI_SCALE = 0xD9;
    char CAL_BI_SCALE = 0xD9;
    char CAL_ACP_H = 0xD4;
    char CAL_ACP_L = 0xD3;
    char CAL_BCP = 0xD5;
    char CAL_TGC = 0xD8;
    char CAL_A0_H = 0xE1;
    char CAL_A0_L = 0xE0;
    char CAL_DELTA_A_SCALE = 0xE3;

    float a_ij[64], b_ij[64], alpha_ij[64];
    unsigned char lsb, msb;
    int resolution, i;
    mlx90621_read_config(&lsb, &msb);
    resolution = (((int) (msb << 8) | lsb) & 0x30) >> 4;

    int cal_a0_h_val = EEPROM[CAL_A0_H];
    int cal_a0_l_val = EEPROM[CAL_A0_L];
    int cal_a0_scale_val = EEPROM[CAL_A0_SCALE];
    int cal_delta_a_scale_val = EEPROM[CAL_DELTA_A_SCALE];
/*
    printf("CAL_EMIS_H: %d \n", EEPROM[CAL_EMIS_H]);
    printf("CAL_EMIS_L: %d \n", EEPROM[CAL_EMIS_L]);
    printf("CAL_ACOMMON_H: %d \n", EEPROM[CAL_ACOMMON_H]);
    printf("CAL_ACOMMON_L: %d \n", EEPROM[CAL_ACOMMON_L]);
    printf("CAL_alphaCP_H: %d \n", EEPROM[CAL_alphaCP_H]);
    printf("CAL_alphaCP_L: %d \n", EEPROM[CAL_alphaCP_L]);
    printf("CAL_AI_SCALE: %d \n", EEPROM[CAL_AI_SCALE]);
    printf("CAL_BI_SCALE: %d \n", EEPROM[CAL_BI_SCALE]);
    printf("CAL_ACP_H: %d \n", EEPROM[CAL_ACP_H]);
    printf("CAL_ACP_L: %d \n", EEPROM[CAL_ACP_L]);
    printf("CAL_BCP: %d \n", EEPROM[CAL_BCP]);
    printf("CAL_TGC: %d \n", EEPROM[CAL_TGC]);
*/
    //Calculate variables from EEPROM
    float emissivity = (256 * EEPROM[CAL_EMIS_H] + EEPROM[CAL_EMIS_L])
            / 32768.0;

    int a_common = 56 * EEPROM[CAL_ACOMMON_H]
            + EEPROM[CAL_ACOMMON_L];
    float alpha_cp = (256 * EEPROM[CAL_alphaCP_H] + EEPROM[CAL_alphaCP_L])
            / (pow(cal_a0_scale_val, 2) * pow((3 - resolution), 2));
    int a_i_scale = (EEPROM[CAL_AI_SCALE] & 0xF0) >> 4;
    int b_i_scale = EEPROM[CAL_BI_SCALE] & 0x0F;
    float a_cp = (float) 256 * EEPROM[CAL_ACP_H] + EEPROM[CAL_ACP_L];
    float b_cp = (float) EEPROM[CAL_BCP];
    float tgc = (signed char) EEPROM[CAL_TGC];
    float v_cp_off_comp = (float) vcp - (a_cp + b_cp * (ta - 25.0));
    float v_ir_off_comp, v_ir_tgc_comp, v_ir_norm, v_ir_comp;
    if (a_common >= 32768)
        a_common -= 65536;
    if (a_cp >= 32768.0)
        a_cp -= 65536.0;
    a_cp /= pow((3 - resolution), 2);
    if (b_cp > 127.0)
        b_cp -= 256.0;
    b_cp /= (pow(b_i_scale, 2) * pow((3 - resolution), 2));
    if (tgc > 127.0)
        tgc -= 256.0;
    tgc /= 32.0;

/*
    printf("CAL_A0_H: %d \n", cal_a0_h_val);
    printf("CAL_A0_L: %d \n", cal_a0_l_val);
    printf("CAL_A0_SCALE: %d \n", cal_a0_scale_val);
    printf("CAL_DELTA_A_SCALE: %d \n", cal_delta_a_scale_val );

    printf("emissivity: %f \n", emissivity);
    printf("a_common: %d \n", a_common);
    printf("alpha_cp: %f \n", alpha_cp);
    printf("a_i_scale: %d \n", a_i_scale);
    printf("b_i_scale: %d \n", b_i_scale);
    printf("a_cp: %f \n", a_cp);
    printf("b_cp: %f \n", b_cp);
    printf("tgc: %f \n", tgc);
    printf("v_cp_off_comp: %f \n", v_cp_off_comp);
*/
    for (i = 0; i < 64; i++) {
        a_ij[i] = ((float) a_common + EEPROM[i] * pow(a_i_scale, 2))
                / pow((3 - resolution), 2);
        //printf("a_ij %d: %f \n", i, a_ij[i]);
        b_ij[i] = EEPROM[0x40 + i];
        if (b_ij[i] > 127)
            b_ij[i] -= 256;
        b_ij[i] = b_ij[i] / (pow(b_i_scale, 2) * pow((3 - resolution), 2));
        //printf("b_ij %d: %f \n", i, b_ij[i]);
        v_ir_off_comp = irData[i] - (a_ij[i] + b_ij[i] * (ta - 25.0));
        //printf("v_ir_off_comp %d: %f \n", i, v_ir_off_comp);
        v_ir_tgc_comp = v_ir_off_comp - tgc * v_cp_off_comp;
        //printf("v_ir_tgc_comp %d: %f \n", i, v_ir_tgc_comp);


        //printf("test1 %d: %9.6f \n", i, (256.0 * cal_a0_h_val + cal_a0_l_val));
        //printf("test2 %d: %9.6f \n", i, pow(cal_a0_scale_val, 2));
        alpha_ij[i] = (float) ((256.0 * cal_a0_h_val + cal_a0_l_val)
                / pow(cal_a0_scale_val, 2));
        //printf("alpha_ij %d: %9.6f \n", i, alpha_ij[i]);
        alpha_ij[i] += (float) (EEPROM[0x80 + i] / pow(cal_delta_a_scale_val, 2));
        //printf("alpha_ij %d: %9.6f \n", i, alpha_ij[i]);
        alpha_ij[i] = (float) alpha_ij[i] / pow(3 - resolution, 2);
        //printf("alpha_ij %d: %9.6f \n", i, alpha_ij[i]);
        v_ir_norm = v_ir_tgc_comp / (alpha_ij[i] - tgc * alpha_cp);
        //printf("v_ir_norm %d: %f \n", i, v_ir_norm);
        v_ir_comp = v_ir_norm / emissivity;
        //printf("v_ir_comp %d: %f \n", i, v_ir_comp);
        temperatures[i] = exp((log(   (v_ir_comp + pow((ta + 273.15), 4))   )/4.0))
                - 273.15;
        temperaturesInt[i] = (unsigned short)((temperatures[i] + 273.15) * 10.0) ;
        //printf("TE Test Temperatures index: %d value: %f \n", i, temperatures[i]);
        //printf("TE Test TemperaturesInt index: %d value: %d \n", i, temperaturesInt[i]);
    }
}


/* IR data read */

int
mlx90621_ir_read()
{
    const unsigned char ir_whole_frame_read[] = {
        0x02, // command
        0x00, // start address
        0x01, // address step
        0x40  // number of reads
    };
    int i, j;
    bcm2835_i2c_begin();
    bcm2835_i2c_setSlaveAddress(0x60);
    if (!bcm2835_i2c_write_read_rs((char *)&ir_whole_frame_read, 4, ir_pixels, 128)
        == BCM2835_I2C_REASON_OK) return 0;
    for (i = 0; i < 128; i += 2) {
            j = i/2;
            irData[j] = (int) (ir_pixels[i+1] << 8) | ir_pixels[i];
            //printf("TE Test index: %d value: %d \n", j, irData[j]);
    }
    return 1;
}


/* Set all the option flags according to the switches specified.
   Return the index of the first non-option argument.  */

static int
decode_switches (int argc, char **argv)
{
  int c;


  while ((c = getopt_long (argc, argv,
			   "h"	/* help */
			   "V",	/* version */
			   long_options, (int *) 0)) != EOF)
    {
      switch (c)
	{
	case 'V':
	  printf ("mlx %s\n", VERSION);
      exit (0);

	case 'h':
	  usage (0);

	default:
	  usage (EXIT_FAILURE);
	}
    }

  return optind;
}


static int
usage (int status)
{
  printf ("%s - \
\n", program_name);
  printf ("Usage: %s [OPTION]... [FILE]...\n", program_name);
  printf ("\
Options:\n\
  -h, --help                 display this help and exit\n\
  -V, --version              output version information and exit\n\
");
  exit (status);
}
