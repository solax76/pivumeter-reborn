#include <math.h>
#include "../pivumeter.h"
#include "../rpi_ws281x/ws2811.h"

#define GPIO_PIN 12
#define DMA 10
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MAX_LEVEL 32767.0f

static double maxComputedLevel;
static int autoreset_counter;
static int totalNumberOfLeds;
static int channelLeds;
static double levelEachLed;
static int startH;
static int endH;

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

typedef struct RGBColor
{
  unsigned char r;
  unsigned char g;
  unsigned char b;
} RGBColor;
/**
 * @brief 
 * 
 * @param H range 0->360
 * @param S range 0->1
 * @param V range 0->1
 * @return RGBColor 
 */
RGBColor convertHSV2RGB(double H, double S, double V)
{
  double r, g, b;

  double h = H / 360.0;
  double s = S;
  double v = V;

  int i = floor(h * 6.0);
  double f = h * 6.0 - i;
  double p = v * (1.0 - s);
  double q = v * (1.0 - f * s);
  double t = v * (1.0 - (1.0 - f) * s);

  switch (i % 6)
  {
  case 0:
    r = v, g = t, b = p;
    break;
  case 1:
    r = q, g = v, b = p;
    break;
  case 2:
    r = p, g = v, b = t;
    break;
  case 3:
    r = p, g = q, b = v;
    break;
  case 4:
    r = t, g = p, b = v;
    break;
  case 5:
    r = v, g = p, b = q;
    break;
  }

  RGBColor color;
  color.r = r * 255.0;
  color.g = g * 255.0;
  color.b = b * 255.0;

  return color;
}

static void clearLedsArray(void)
{
  int x;
  for (x = 0; x < totalNumberOfLeds; x++)
  {
    ws2811_data.channel[0].leds[x] = 0;
  }
}

static void renderLedsArray()
{
  ws2811_return_t ret;
  if ((ret = ws2811_render(&ws2811_data)) != WS2811_SUCCESS)
  {
    fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
  }
}

static void stopRenderingLeds(void)
{
  clearLedsArray();
  renderLedsArray();
  ws2811_fini(&ws2811_data);
}


static double computeLedValue(int led, double meter_value)
{
  double threshold = (MAX_LEVEL * (led + 1)) / channelLeds;
  double value;
  if (meter_value >= threshold)
  {
    return 1.0;
  }
  else
  {
    value = (meter_value - threshold + levelEachLed) / levelEachLed;
    if (value > 1.0)
    {
      value = 1.0;
    }
    else if (value < 0)
    {
      value = 0.0;
    }
  }
  return value;
}
static void init_colors()
{
  // fprintf(stderr,"Init blinkt vumeter colors\n");
  startH = rand() % 360;
  endH = rand() % 360;
}
static void setLedArrayPixelColor(unsigned char index, RGBColor color)
{
  if (index < totalNumberOfLeds)
  {
    int pixelColor = color.r << 16;
    pixelColor |= color.g << 8;
    pixelColor |= color.b;
    ws2811_data.channel[0].leds[index] = pixelColor;
  }
}

/**
 * @brief Metodo di interfaccia che viene chiamato all'inizializzazione del sistema
 *
 * @return int
 */
static int ws2812_ring_init(void)
{
  maxComputedLevel = 0;
  channelLeds = totalNumberOfLeds / 2;
  autoreset_counter = 0;
  levelEachLed = MAX_LEVEL / channelLeds;

  // la funzione di init crea l'array dei leds
  ws2811_data.channel[0].count = totalNumberOfLeds;
  ws2811_return_t ret;
  if ((ret = ws2811_init(&ws2811_data)) != WS2811_SUCCESS)
  {
    fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
    return ret;
  }
  init_colors();
  clearLedsArray();
  renderLedsArray();

  atexit(stopRenderingLeds);
  return 0;
}
/**
 * @brief Funzione di aggiornamento che riporta il livello dei due canali
 *
 * @param meter_level_l
 * @param meter_level_r
 * @param level
 */
static void ws2812_ring_update(int meter_level_l, int meter_level_r, snd_pcm_scope_ameter_t *level)
{
  // calcolo il max del livello per utilizzare tutta la barra utile anche se il livello è basso
  maxComputedLevel = MAX(maxComputedLevel, meter_level_l);
  maxComputedLevel = MAX(maxComputedLevel, meter_level_r);
  double level_scale = maxComputedLevel / MAX_LEVEL;
  // scalo i valori di livello per averli sempre da [0..MAX_LEVEL] a prescindere dal volume selezionato
  // dal sistema audio.
  if (level_scale > 0)
  {
    meter_level_l = meter_level_l / level_scale;
    meter_level_r = meter_level_r / level_scale;
  }

  // ora per ogni led calcolo i colori e luminosità
  // definito N il numero di led totali, una metà verrà usato per il canale destro,
  // l'altra per il canale sinistro
  int led;
  double led_H, led_H_right, led_H_left; //[0..360]
  double led_S;                          //[0..1]
  double led_V;                          //[0..1]
  double led_value;
  RGBColor color;

  // dati fissi per tutti i led
  double b_factor = level->led_brightness / 255.0; // -> converto a range [0--1]
  if (maxComputedLevel > 0)
  {
    led_H_right = 360.0 * meter_level_r / maxComputedLevel;
    led_H_left = 360.0 * meter_level_l / maxComputedLevel;
  }

  // ora calcolo i valori dei singoli led
  for (led = 0; led < totalNumberOfLeds; led++)
  {
    double index;

    if (led < channelLeds)
    {
      // Right channel
      index = led;
      led_value = computeLedValue(index, meter_level_r);
      led_V = b_factor * led_value;
      led_S = led_value;
      led_H = endH + (startH-endH)*(index+1)/(double)channelLeds;
    }
    else
    {
      // Left channel
      index = totalNumberOfLeds - led;
      led_value = computeLedValue(index, meter_level_l);
      led_V = b_factor * led_value;
      led_S = led_value;
      led_H = endH + (startH-endH)*(index+1)/(double)channelLeds;
    }

    if (level->bar_reverse == 1)
    {
      index = totalNumberOfLeds - index;
    }

    // b_scale [0->1]
    // value   [0->1]
    color = convertHSV2RGB(led_H, led_S, led_V);
    setLedArrayPixelColor(led, color);
  }

  renderLedsArray();

  autoreset_counter++;
  if (autoreset_counter > 10000)
  {
    autoreset_counter = 0;
    maxComputedLevel = MAX(meter_level_l, meter_level_r);
    init_colors();
  } else if (autoreset_counter%100==0) {
    startH = ((startH+1)%360);
    endH = ((endH+1)%360);
  }
}
/**
 * @brief Metodo di costruzione chiamato da pivumeter.c
 *
 * @param leds
 * @return device
 */
device ws2812_strip(int numberOfLeds)
{
  struct device _ws2812_ring;
  totalNumberOfLeds = numberOfLeds;
  _ws2812_ring.init = &ws2812_ring_init;
  _ws2812_ring.update = &ws2812_ring_update;
  return _ws2812_ring;
}
