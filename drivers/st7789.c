#include "drivers/st7789.h"

#define CMD(x)    st7789_send_cmd(x)
#define DATA(x,y) st7789_send_data(x,y)
#define BYTE(x)   st7789_send_data_byte(x)
#define WAIT(x)   st7789_wait(x)

#define WIDTH     240
#define HEIGHT    240
#define BPP       12
#define FB_SIZE   ((BPP*WIDTH*HEIGHT)/8)
#define FB_CHUNK_SIZE (ST779_PARALLEL_LINES * (BPP*WIDTH)/8)

#define P1MASK    0xFFFF0F00
#define P1MASKP   0x0000F0FF
#define P2MASK    0xFF00F0FF
#define P2MASKP   0x00FF0F00

spi_device_handle_t spi;
ledc_timer_config_t backlight_timer = {
  .duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty
  .freq_hz = 5000,                      // frequency of PWM signal
  .speed_mode = LEDC_HIGH_SPEED_MODE,           // timer mode
  .timer_num = LEDC_TIMER_0,            // timer index
  .clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
};

ledc_channel_config_t backlight_config = {
  .channel    = LEDC_CHANNEL_0,
  .duty       = 0,
  .gpio_num   = ST7789_BL_IO,
  .speed_mode = LEDC_HIGH_SPEED_MODE,
  .hpoint     = 0,
  .timer_sel  = LEDC_TIMER_0
};

const bool g_inv_x = true;
const bool g_inv_y = true;

DRAM_ATTR static uint8_t databuf[16];

/* Framebuffer. We need one more byte to handle pixels with 32-bit values. */
__attribute__ ((aligned(4)))
DRAM_ATTR static uint8_t framebuffer[FB_SIZE];

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} init_cmd_t;

DRAM_ATTR static const init_cmd_t st_init_cmds[]={
  {ST7789_CMD_SLPOUT, {0}, 0},
  {ST7789_CMD_WAIT, {0}, 250},
  {ST7789_CMD_COLMOD, {0x03}, 1}, /* COLMOD: 12 bits per pixel, 4K colors */
  {ST7789_CMD_WAIT, {0}, 10},
  {ST7789_CMD_CASET, {0x00, 0x00, 0x00, 0xF0}, 4},
  {ST7789_CMD_RASET, {0x00, 0x00, 0x00, 0xF0}, 4},
  {ST7789_CMD_INVON, {0x00}, 0},
  {ST7789_CMD_WAIT, {0}, 10},
  {ST7789_CMD_NORON, {0x00}, 0},
  {ST7789_CMD_WAIT, {0}, 10},
  {ST7789_CMD_DISPON, {0x00}, 0},
  {ST7789_CMD_WAIT, {0}, 250},
  {0,{0}, 0xff}
};

/**
 * @brief Wait for given milliseconds
 * @param milliseconds: nimber of milliseconds to wait
 **/

void st7789_wait(int milliseconds)
{
  vTaskDelay(milliseconds/portTICK_PERIOD_MS);
}

void st7789_pre_transfer_callback(spi_transaction_t *t)
{
    int dc=(int)t->user;
    gpio_set_level(ST7789_SPI_DC_IO, dc);
}

esp_err_t st7789_send_cmd(const uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=8;                     //Command is 8 bits
    t.tx_buffer=&cmd;               //The data is the cmd itself
    t.user=(void*)0;                //D/C needs to be set to 0
    ret = spi_device_polling_transmit(spi, &t);  //Transmit!
    assert(ret==ESP_OK);
    return ret;
}

esp_err_t st7789_send_data(const uint8_t *data, int len)
{
    esp_err_t ret;
    spi_transaction_t t;
    if (len==0) return ESP_FAIL;             //no need to send anything
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=len*8;                 //Len is in bytes, transaction length is in bits.
    t.tx_buffer=data;               //Data
    t.user=(void*)1;                //D/C needs to be set to 1
    ret = spi_device_polling_transmit(spi, &t);  //Transmit!
    assert(ret==ESP_OK);
    return ret;
}

esp_err_t st7789_send_data_byte(const uint8_t byte)
{
  return st7789_send_data(&byte, 1);
}


void st7789_init_display(void)
{
  int cmd = 0;

  /* Execute initialization sequence. */
  while (st_init_cmds[cmd].databytes!=0xff) {
    if (st_init_cmds[cmd].cmd == ST7789_CMD_WAIT)
    {
      vTaskDelay(st_init_cmds[cmd].databytes / portTICK_RATE_MS);
    }
    else
    {
      st7789_send_cmd(st_init_cmds[cmd].cmd);
      st7789_send_data(st_init_cmds[cmd].data, st_init_cmds[cmd].databytes&0x1F);
    }
    cmd++;
  }
}


/**
 * @brief Initializes the ST7789 display
 * @retval ESP_OK on success, ESP_FAIL otherwise.
 **/

esp_err_t st7789_init(void)
{
  spi_bus_config_t bus_config = {
        .miso_io_num=-1,
        .mosi_io_num=ST7789_SPI_MOSI_IO,
        .sclk_io_num=ST7789_SPI_SCLK_IO,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz=ST779_PARALLEL_LINES*240*2+8,
    };

  spi_device_interface_config_t devcfg={
        .clock_speed_hz=ST7789_SPI_SPEED,
        .mode=0,
        .spics_io_num=ST7789_SPI_CS_IO,
        .queue_size=7,                          //We want to be able to queue 7 transactions at a time
        .pre_cb=st7789_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
        .flags=SPI_DEVICE_HALFDUPLEX|SPI_DEVICE_NO_DUMMY
    };


  /* Initialize our SPI interface. */
  if (spi_bus_initialize(HSPI_HOST, &bus_config, ST7789_DMA_CHAN) == ESP_OK)
  {
      if (spi_bus_add_device(HSPI_HOST, &devcfg, &spi) == ESP_OK)
      {
        /* Initialize GPIOs */
        gpio_pad_select_gpio(ST7789_BL_IO);
        gpio_pad_select_gpio(ST7789_SPI_DC_IO);
        gpio_pad_select_gpio(ST7789_SPI_CS_IO);
        gpio_set_direction(ST7789_BL_IO, GPIO_MODE_OUTPUT);
        gpio_set_direction(ST7789_SPI_DC_IO, GPIO_MODE_OUTPUT);
        gpio_set_direction(ST7789_SPI_CS_IO, GPIO_MODE_OUTPUT);

        /* Configure backlight for PWM (light control) */
        ledc_timer_config(&backlight_timer);
        ledc_channel_config(&backlight_config);

        /* Set default duty cycle (0, backlight is off). */
        ledc_set_duty(backlight_config.speed_mode, backlight_config.channel, 5000);
        ledc_update_duty(backlight_config.speed_mode, backlight_config.channel);

        vTaskDelay(100 / portTICK_PERIOD_MS);

        /* Send init commands. */
        st7789_init_display();

        return ESP_OK;
      }
      else
        return ESP_FAIL;
  }
  else
    return ESP_FAIL;
}


/**
 * @brief Set screen backlight to max.
 **/

void st7789_backlight_on(void)
{
  ledc_set_duty(backlight_config.speed_mode, backlight_config.channel, 5000);
  ledc_update_duty(backlight_config.speed_mode, backlight_config.channel);
}


/**
 * @brief Set screen backlight level.
 * @param backlight_level, from 0 to 5000
 **/

void st7789_backlight_set(int backlight_level)
{
  ledc_set_duty(backlight_config.speed_mode, backlight_config.channel, backlight_level);
  ledc_update_duty(backlight_config.speed_mode, backlight_config.channel);
}


void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  databuf[0] = x0 >> 8;
  databuf[1] = x0 & 0xFF;
  databuf[2] = x1 >> 8;
  databuf[3] = x1 & 0xFF;
  CMD(ST7789_CMD_CASET);
  DATA(databuf, 4);
  databuf[0] = y0 >> 8;
  databuf[1] = y0 & 0xFF;
  databuf[2] = y1 >> 8;
  databuf[3] = y1 & 0xFF;
  CMD(ST7789_CMD_RASET);
  DATA(databuf, 4);
  CMD(ST7789_CMD_RAMWR);
}

void st7789_set_fb(uint8_t *frame)
{
  memcpy(framebuffer, frame, FB_SIZE);
}


/**
 * @brief Send framebuffer to screen.
 **/

void st7789_commit_fb(void)
{
  int i;
  st7789_set_window(0, 0, WIDTH, HEIGHT);
  for (i=0; i<FB_SIZE/FB_CHUNK_SIZE; i++)
  {
    st7789_send_data(&framebuffer[i*FB_CHUNK_SIZE], FB_CHUNK_SIZE);
  }
}


/**
 * @brief Fill screen with default color (black)
 **/

void st7789_blank(void)
{
  memset(framebuffer, 0, FB_SIZE);
}


/**
 * @brief Set a pixel color in framebuffer
 * @param x: pixel X coordinate
 * @param y: pixel Y coordinate
 * @param color: pixel color (12 bits)
 **/

void st7789_set_pixel(int x, int y, uint16_t color)
{
  int fb_blk, fb_blk_off;
  uint32_t *ppixel;
  uint32_t *ppixel2;

  /* Sanity checks. */
  if ((x<0) || (x>=WIDTH) || (y<0) || (y>=HEIGHT))
    return;

  /* Invert if required. */
  if (g_inv_x)
    x = WIDTH - x;
  if (g_inv_y)
    y = HEIGHT - y;

  /* 4-pixel block index */
  fb_blk = (y*WIDTH+x)/4;
  /* Compute address of our 4-pixel block (stored on 6 bytes). */
  fb_blk_off = fb_blk*6;

  //printf("[enter setpixel] (%d,%d) color %03x\r\n", x,y,color);
  //printf(" fb_blk=%d\r\n", fb_blk);
  //printf(" fb_blk_off=%d\r\n", fb_blk_off);

  /* Modify pixel by 4-byte blocks, as ESP32 does not allow byte-level access. */
  switch(x%4)
  {
    /**
     * Case 0: pixel is stored in byte 0 and 1 of a 4-byte dword.
     * pixel is 0B RG
     * RG BX XX XX
     **/
    case 0:
      {
        ppixel = (uint32_t *)(&framebuffer[fb_blk_off]);
        //printf("[screen] color: %03x\r\n", color);
        //printf("[screen] (before) 32-bit data: %08x\r\n", *ppixel);
        *ppixel = (*ppixel & 0xffff0f00) | (color & 0x00ff) | ((color&0xf00)<<4);
        //printf("[screen] (now)    32-bit data: %08x\r\n", *ppixel);
      }
      break;

    /**
     * Case 1: pixel is stored in byte 1 and 2 of a 4-byte dword.
     * pixel is 0B RG
     * XX XR GB XX
     **/

    case 1:
      {
        ppixel = (uint32_t *)(&framebuffer[fb_blk_off]);
        *ppixel = (*ppixel & 0xfff00f0ff) | ((color&0xf0)<<4) | ((color&0xf)<<20) | ((color&0xf00)<<8);
      }
      break;

    /**
     * Case 2: pixel is stored in byte 3 and 4 of a double 4-byte dword.
     * pixel is 0B RG
     * XX XX XX RG | BX XX XX XX
     **/

    case 2:
      {
        ppixel = (uint32_t *)(&framebuffer[fb_blk_off]);
        ppixel2 = (uint32_t *)(&framebuffer[fb_blk_off+4]);
        *ppixel = (*ppixel & 0x00ffffff) | (color&0xff)<<24;
        *ppixel2 = (*ppixel2 & 0xffffff0f) | (color&0xf00)>>4;
      }
      break;

    /**
     * Case 3: pixel is stored in byte 4 and 5 of a double 4-byte dword.
     * pixel is 0B RG
     * XX XX XX XX | XR GB XX XX
     **/

    case 3:
      {
        ppixel = (uint32_t *)(&framebuffer[fb_blk_off+4]);
        *ppixel = (*ppixel & 0xffff00f0) | (color&0xf0)>>4 | (color&0xf)<<12 | (color&0xf00);
      }
      break;
  }
}


/**
 * @brief Fills a region of the screen with a specific color
 * @param x: top-left X coordinate
 * @param y: top-left Y coordinate
 * @param width: region width
 * @param height: region height
 * @parma color: 12 bpp color
 **/

void st7789_fill_region(int x, int y, int width, int height, uint16_t color)
{
  int _x,_y;

  /* X and y cannot be less than zero. */
  if (x<0)
  {
    /* Fix width, exit if region is out of screen. */
    width += x;
    if (width <= 0)
      return;
    x=0;
  }

  if (y<0)
  {
    /* Fix height, exit if region is out of screen. */
    height += y;
    if (height <= 0)
      return;
    y=0;
  }

  /* Region must not exceed screen size. */
  if ((x+width) > WIDTH)
    width = WIDTH-x;
  if ((y+height) > HEIGHT)
    height = HEIGHT-y;

  for (_x=x; _x<(x+width); _x++)
  {
    for (_y=y; _y<(y+height); _y++)
    {
      st7789_set_pixel(_x, _y, color);
    }
  }
}

/**
 * @brief Draw a line of color `color` between (x0,y0) and (x1, y1)
 * @param x0: X coordinate of the start of the line
 * @param y0: Y cooordinate of the start of the line
 * @param x1: X coordinate of the end of the line
 * @param y1: y coordinate of the end of the line
 **/
void st7789_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
  int x, y, dx, dy;
  float e, ex, ey;

  dy = y1 - y0;
  dx = x1 - x0;

  /* Vertical line */
  if (dx == 0)
  {
    /* Make sure y0 <= y1. */
    if (y0>y1)
    {
      dy = y0;
      y0 = y1;
      y1 = dy;
    }

    for (y=y0; y<y1; y++)
      st7789_set_pixel(x0, y, color);
  }
  else
  {
    y = y0;
    e = 0.0;
    ex = dy/dx;
    ey = -1.0;
    for (x=x0; x<=x1; x++)
    {
      st7789_set_pixel(x, y, color);
      e += ex;
      if (e >= 0.5)
      {
        y++;
        e = e+ey;
      }
    }
  }
}
