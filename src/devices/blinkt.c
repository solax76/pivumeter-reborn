#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include "../pivumeter.h"

#define DAT 23
#define CLK 24
#define NUM_PIXELS 8
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

static int red_1;
static int green_1;
static int blue_1;

static int red_2;
static int green_2;
static int blue_2;

static double delta_s1;
static double delta_s2;
static double m1;
static double m2;
static double delta_r;
static double delta_g;
static double delta_b;

static unsigned int pixels[NUM_PIXELS] = {0,0,0,0,0,0,0,0};
static double max_level;
static int autoreset_counter;

static void blinkt_set_pixel(unsigned char index, unsigned char r, unsigned char g, unsigned char b, unsigned char brightness){
    pixels[index] = (brightness & 31);
    pixels[index] <<= 5;
    pixels[index] |= r;
    pixels[index] <<= 8;
    pixels[index] |= g;
    pixels[index] <<= 8;
    pixels[index] |= b;
}

static void blinkt_write_byte(unsigned char byte){
    int x;
    for(x = 0; x<8; x++){
        digitalWrite(DAT, byte & 0b10000000);
        digitalWrite(CLK, 1);
        byte <<= 1;
        asm("NOP");
        asm("NOP");
        asm("NOP");
        digitalWrite(CLK, 0);
    }
}

static void blinkt_sof(void){
    int x;
    for(x = 0; x<4; x++){
        blinkt_write_byte(0);
    }
}

static void blinkt_eof(void){
    int x;
    for(x = 0; x<5; x++){
        blinkt_write_byte(0);
    }
}

static void blinkt_show(void){
    int x;
    blinkt_sof();
    for(x = 0; x < NUM_PIXELS; x++){
        unsigned char r, g, b, brightness;
        brightness = (pixels[x] >> 22) & 31;
        r = (pixels[x] >> 16) & 255;
        g = (pixels[x] >> 8)  & 255;
        b = (pixels[x])       & 255;
        blinkt_write_byte(0b11100000 | brightness);
        blinkt_write_byte(b);
        blinkt_write_byte(g);
        blinkt_write_byte(r);
    }
    blinkt_eof();
}
static void blinkt_clear_display(void){
    int x;
    for(x = 0; x < NUM_PIXELS; x++){
        pixels[x] = 0;
    }
    blinkt_show();
}

static void init_colors(){
    //fprintf(stderr,"Init blinkt vumeter colors\n");
    red_1 = 0;
    green_1 = rand() % 256;
    blue_1 = rand() % 256;

    red_2 = rand() % 256;
    green_2 = 0;
    blue_2 = rand() % 256;

    delta_s1 = 32767.0f/NUM_PIXELS;
    delta_s2 = 32767.0f/4.0;
    m1 = 255.0/delta_s1;
    m2 = -255.0/delta_s2;

    delta_r = (red_2-red_1)/NUM_PIXELS;
    delta_g = (green_2-green_1)/NUM_PIXELS;
    delta_b = (blue_2-blue_1)/NUM_PIXELS;
}

static int blinkt_init(void){
    system("gpio export 23 output");
    system("gpio export 24 output");
    wiringPiSetupSys();
    max_level = 0;
    autoreset_counter=0;

    blinkt_clear_display();

    atexit(blinkt_clear_display);

    init_colors();
    return 0;
}

static double get_led_level_value(int led, double meter_value) {
    double s = (32767.0f*(led+1))/NUM_PIXELS;
    double value;
    if (meter_value < s) {
        // salita
        value = m1 * (meter_value - s + delta_s1);
    } else {
        // discesa
        value = m2 * (meter_value - s - delta_s2);
    }
    if (value > 255.0) {
        value = 255.0;
    } else if (value < 0) {
        value = 0;
    }
    return value;
}

static void blinkt_update(int meter_level_l, int meter_level_r, snd_pcm_scope_ameter_t *level){
    int x;
    for(x = 0; x < NUM_PIXELS; x++){
        pixels[x] = 0;
    }
    // scala della luminosità
    double b_scale = level->led_brightness/255.0;
    // calcolo il max del livello per utilizzare tutta la barra utile anche se il livello è basso
    max_level = MAX(max_level, meter_level_l);
    max_level = MAX(max_level, meter_level_r);
    double level_scale = max_level/32767.0f;
    if (level_scale > 0) {
        meter_level_l = meter_level_l/level_scale;
        meter_level_r = meter_level_r/level_scale;
    }

    int led;
    for(led = 0; led < NUM_PIXELS; led++){

        int index_l = 7-led;
        int index_r = led;
        if(level->bar_reverse == 1){
            index_l = led;
            index_r = 7-led;
        }
        double value_l = get_led_level_value(index_l, meter_level_l);
        double value_r = get_led_level_value(index_r, meter_level_r);

        int RL = b_scale*value_l*(delta_r*index_l + red_1)/255.0;
        int GL = b_scale*value_l*(delta_g*index_l + green_1)/255.0;
        int BL = b_scale*value_l*(delta_b*index_l + blue_1)/255.0;

        int RR = b_scale*value_r*(delta_r*index_r + red_1)/255.0;
        int GR = b_scale*value_r*(delta_g*index_r + green_1)/255.0;
        int BR = b_scale*value_r*(delta_b*index_r + blue_1)/255.0;

        blinkt_set_pixel(index_r, MAX(RL,RR), MAX(GL,GR) , MAX(BL,BR), 16);
    }

    blinkt_show();
    autoreset_counter++;
    if (autoreset_counter > 10000){
        autoreset_counter=0;
        max_level=0;
        init_colors();
    }
}

device blinkt(){
    struct device _blinkt;
    _blinkt.init = &blinkt_init;
    _blinkt.update = &blinkt_update;
    return _blinkt;
}
