#include "../pivumeter.h"
#include "../rpi_ws281x/ws2811.h"

#define GPIO_PIN 12
#define DMA 10
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

static double max_level;
static int autoreset_counter;
static int totalLeds;

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

static ws2811_t ws2811_data = {
    .freq = WS2811_TARGET_FREQ,
    .dmanum = DMA,
    .channel =
        {
            [0] =
                {
                    .gpionum = GPIO_PIN,
                    .invert = 0,
                    .count = 0, //-> va impostato nell'init
                    .strip_type = WS2811_STRIP_BGR,
                    .brightness = 255,
                },
            [1] =
                {
                    .gpionum = 0,
                    .invert = 0,
                    .count = 0,
                    .brightness = 0,
                },
        },
};
// static unsigned int pixels[NUM_PIXELS] = {0,0,0,0,0,0,0,0};

static void leds_array_clear(void)
{
  int x;
  for (x = 0; x < totalLeds; x++)
  {
    ws2811_data.channel[0].leds[x] = 0;
  }
}

static void leds_array_render()
{
  ws2811_return_t ret;
  if ((ret = ws2811_render(&ws2811_data)) != WS2811_SUCCESS)
  {
    fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
  }
}

static void init_colors()
{
  // fprintf(stderr,"Init blinkt vumeter colors\n");
  red_1 = 0;
  green_1 = rand() % 256;
  blue_1 = rand() % 256;

  red_2 = rand() % 256;
  green_2 = 0;
  blue_2 = rand() % 256;

  m1 = 255.0 / delta_s1;
  m2 = -255.0 / delta_s2;

  delta_r = (red_2 - red_1) / totalLeds;
  delta_g = (green_2 - green_1) / totalLeds;
  delta_b = (blue_2 - blue_1) / totalLeds;
}

static double get_led_level_value(int led, double meter_value)
{
  double s = (32767.0f * (led + 1)) / totalLeds;
  double value;
  if (meter_value < s)
  {
    // salita
    value = m1 * (meter_value - s + delta_s1);
    fprintf(stderr, "get_led_level_value salita per led %d, meter_value %f, delta_s1 %f, s %f , m1 %f => value %f\n", led, meter_value, delta_s1, s,m1, value);
  }
  else
  {
    // discesa
    value = m2 * (meter_value - s - delta_s2);
    fprintf(stderr, "get_led_level_value discesa per led %d, meter_value %f, delta_s2 %f, s %f, m2 %f  => value %f\n", led, meter_value, delta_s2, s,m2, value);
  }
  if (value > 255.0)
  {
    value = 255.0;
  }
  else if (value < 0)
  {
    value = 0;
  }
  return value;
}

static void leds_array_set_pixel(unsigned char index, unsigned char r, unsigned char g, unsigned char b){
  if (index<totalLeds){
    int pixelColor = r << 16;
    pixelColor |= g << 8;
    pixelColor |= b;
    ws2811_data.channel[0].leds[index] = pixelColor;
  }
}

static int ws2812_ring_init(void)
{
  // system("gpio export 23 output");
  // system("gpio export 24 output");
  //  wiringPiSetupSys();
  max_level = 0;
  autoreset_counter = 0;
  delta_s1 = 32767.0f / totalLeds;
  delta_s2 = 32767.0f / 4.0;

  // la funzione di init crea l'array dei leds
  ws2811_data.channel[0].count = totalLeds;
  ws2811_return_t ret;
  if ((ret = ws2811_init(&ws2811_data)) != WS2811_SUCCESS)
  {
    fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
    return ret;
  }
  leds_array_clear();
  leds_array_render();
  return 0;
}

static void ws2812_ring_update(int meter_level_l, int meter_level_r, snd_pcm_scope_ameter_t *level)
{
  leds_array_clear();

  // scala della luminosità
  double b_scale = level->led_brightness / 255.0;
  // calcolo il max del livello per utilizzare tutta la barra utile anche se il livello è basso
  max_level = MAX(max_level, meter_level_l);
  max_level = MAX(max_level, meter_level_r);
  double level_scale = max_level / 32767.0f;
  if (level_scale > 0)
  {
    meter_level_l = meter_level_l / level_scale;
    meter_level_r = meter_level_r / level_scale;
  }

  int led;
  for (led = 0; led < totalLeds; led++)
  {

    int index_l = totalLeds - led;
    int index_r = led;
    if (level->bar_reverse == 1)
    {
      index_l = led;
      index_r = totalLeds - led;
    }
    double value_l = get_led_level_value(index_l, meter_level_l);
    double value_r = get_led_level_value(index_r, meter_level_r);

    int RL = b_scale * value_l * (delta_r * index_l + red_1) / 255.0;
    int GL = b_scale * value_l * (delta_g * index_l + green_1) / 255.0;
    int BL = b_scale * value_l * (delta_b * index_l + blue_1) / 255.0;

    int RR = b_scale * value_r * (delta_r * index_r + red_1) / 255.0;
    int GR = b_scale * value_r * (delta_g * index_r + green_1) / 255.0;
    int BR = b_scale * value_r * (delta_b * index_r + blue_1) / 255.0;

    leds_array_set_pixel(index_r, MAX(RL, RR), MAX(GL, GR), MAX(BL, BR));
  }

  leds_array_render();
  autoreset_counter++;
  if (autoreset_counter > 10000)
  {
    autoreset_counter = 0;
    max_level = 0;
    init_colors();
  }
}

device ws2812_ring(int leds)
{
  struct device _ws2812_ring;
  totalLeds = leds;
  _ws2812_ring.init = &ws2812_ring_init;
  _ws2812_ring.update = &ws2812_ring_update;
  return _ws2812_ring;
}
/*
void writeColor(int val, ws2811_t *ledstring)
{
  int i;
  for (i = 0; i < sizeof(digit1); i++)
  {
    (*ledstring).channel[0].leds[i] = val;
  }
}

void render()
{
  if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS)
  {
    fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
    // break;
  }
}

int main(int argc, char *argv[])
{
    fprintf(stdout, "Version " VERSION_STR);
    //matrix = malloc(sizeof(ws2811_led_t) * width * height);

    if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS)
    {
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
        return ret;
    }

    writeled (digit1, &ledstring); render(); sleep(1);
    writeled (digit2, &ledstring); render(); sleep(1);
    writeled (digitTest, &ledstring); render();   sleep (1);

    writeColor (0xFF0000, &ledstring); render(); sleep (1);

    writeColor (0x00FF00, &ledstring); render(); sleep (1);
    writeColor (0x0000FF, &ledstring); render(); sleep (1);

    writeColor (0x00FF00, &ledstring); render(); sleep (1);
    writeColor (0x0000FF, &ledstring); render(); sleep (1);

    writeColor (0x00FF00, &ledstring); render(); sleep (1);
    writeColor (0x0000FF, &ledstring); render(); sleep (1);

    writeColor (0x000000, &ledstring); render();


    ws2811_fini(&ledstring);
    printf ("\n");

    return 0;
}
*/