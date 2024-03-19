#include "../pivumeter.h"
#include "../rpi_ws281x/ws2811.h"

#define GPIO_PIN 12
#define DMA 10
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MAX_LEVEL 32767.0f

static double max_comp_level;
static int autoreset_counter;
static int totalLeds;
static int channelLeds;

static int red_1;
static int green_1;
static int blue_1;

static int red_2;
static int green_2;
static int blue_2;

static double delta_s1;
static double m1;
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
  green_2 = 255;
  blue_2 = rand() % 256;

  m1 = 255.0 / delta_s1;

  delta_r = (red_2 - red_1) / channelLeds;
  delta_g = (green_2 - green_1) / channelLeds;
  delta_b = (blue_2 - blue_1) / channelLeds;
}

static void update_colors()
{
  // fprintf(stderr,"Init blinkt vumeter colors\n");
  red_1 = (red_1+1) % 256;
  green_1 = (green_1+1) % 256;
  blue_1 = (blue_1+1) % 256;

  red_2 = (red_2+1) % 256;
  green_2 = (green_2+1)% 256;
  blue_2 = (blue_2+1) % 256;

  m1 = 255.0 / delta_s1;

  delta_r = (red_2 - red_1) / channelLeds;
  delta_g = (green_2 - green_1) / channelLeds;
  delta_b = (blue_2 - blue_1) / channelLeds;
}

static double get_led_level_value(int led, double meter_value)
{
  double s = (MAX_LEVEL * (led + 1)) / channelLeds;
  double value;
  if (meter_value < s)
  {
    // salita
    value = m1 * (meter_value - s + delta_s1);
    // fprintf(stderr, "get_led_level_value salita per led %d, meter_value %f, delta_s1 %f, s %f , m1 %f => value %f\n", led, meter_value, delta_s1, s,m1, value);
  }
  else
  {
    // superato
    value = 255.0;
    // fprintf(stderr, "get_led_level_value discesa per led %d, meter_value %f, delta_s2 %f, s %f, m2 %f  => value %f\n", led, meter_value, delta_s2, s,m2, value);
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

static void leds_array_set_pixel(unsigned char index, unsigned char r, unsigned char g, unsigned char b)
{
  if (index < totalLeds)
  {
    int pixelColor = r << 16;
    pixelColor |= g << 8;
    pixelColor |= b;
    ws2811_data.channel[0].leds[index] = pixelColor;
  }
}
static void leds_finished(void)
{
  leds_array_clear();
  leds_array_render();
  ws2811_fini(&ws2811_data);
}
static int ws2812_ring_init(void)
{
  // system("gpio export 23 output");
  // system("gpio export 24 output");
  //  wiringPiSetupSys();
  max_comp_level = 0;
  channelLeds = totalLeds / 2;
  autoreset_counter = 0;
  delta_s1 = MAX_LEVEL / channelLeds;

  // la funzione di init crea l'array dei leds
  ws2811_data.channel[0].count = totalLeds;
  ws2811_return_t ret;
  if ((ret = ws2811_init(&ws2811_data)) != WS2811_SUCCESS)
  {
    fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
    return ret;
  }
  init_colors();
  leds_array_clear();
  leds_array_render();
  atexit(leds_finished);
  return 0;
}

static void ws2812_ring_update(int meter_level_l, int meter_level_r, snd_pcm_scope_ameter_t *level)
{
  leds_array_clear();

  // scala della luminosità
  double b_scale = level->led_brightness / 255.0;
  // calcolo il max del livello per utilizzare tutta la barra utile anche se il livello è basso
  max_comp_level = MAX(max_comp_level, meter_level_l);
  max_comp_level = MAX(max_comp_level, meter_level_r);
  double level_scale = max_comp_level / MAX_LEVEL;
  if (level_scale > 0)
  {
    meter_level_l = meter_level_l / level_scale;
    meter_level_r = meter_level_r / level_scale;
  }

  int led;
  for (led = 0; led < totalLeds; led++)
  {
    double index;
    double value;
    if (led < channelLeds)
    {
      // Right channel
      index = led;
      if (level->bar_reverse == 1)
      {
        index = totalLeds - led;
      }
      value = get_led_level_value(led, meter_level_r)/ 255.0;
    }
    else
    {
      // Left channel
      index =totalLeds-led;
      if (level->bar_reverse == 1)
      {
        index = led;
      }
      value = get_led_level_value(totalLeds-led, meter_level_l)/ 255.0;
    }
    // b_scale [0->1]
    // value   [0->1]
    int RL = b_scale * value * (delta_r * index + red_1);
    int GL = b_scale * value * (delta_g * index + green_1);
    int BL = b_scale * value * (delta_b * index + blue_1);

    leds_array_set_pixel(led, RL, GL, BL);
  }

  leds_array_render();
  autoreset_counter++;
  if (autoreset_counter > 10000)
  {
    autoreset_counter = 0;
    max_comp_level = MAX(meter_level_l, meter_level_r);
    init_colors();
  } else if (autoreset_counter % 100 == 0){
    update_colors();
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
