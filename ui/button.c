#include "ui/button.h"

void widget_button_drawfunc(widget_t *p_widget)
{
  int text_width, dx, dy;
  widget_button_t *p_button = (widget_button_t *)p_widget->p_user_data;
  printf("button draw\r\n");
  if (p_button != NULL)
  {
    /* Draw the button. */
    widget_draw_line(
      p_widget,
      1,
      0,
      p_widget->width - 2,
      0,
      BUTTON_STYLE_BORDER
    );
    widget_draw_line(
      p_widget,
      1,
      p_widget->height - 1,
      p_widget->width - 2,
      p_widget->height - 1,
      BUTTON_STYLE_BORDER
    );
    widget_draw_line(
      p_widget,
      0,
      1,
      0,
      p_widget->height - 2,
      BUTTON_STYLE_BORDER
    );
    widget_draw_line(
      p_widget,
      p_widget->width - 1,
      1,
      p_widget->width - 1,
      p_widget->height - 2,
      BUTTON_STYLE_BORDER
    );

    /* Draw button background. */
    if (p_button->state == BUTTON_PRESSED)
    {
      widget_fill_region(
        p_widget,
        1,
        1,
        p_widget->width-2,
        p_widget->height-2,
        BUTTON_STYLE_BG_PRESSED
      );
    }
    else
    {
      widget_fill_region(
        p_widget,
        1,
        1,
        p_widget->width-2,
        p_widget->height-2,
        BUTTON_STYLE_BG
      );
    }

    /* Draw text. */
    if (p_button->psz_label != NULL)
    {
      /* Compute text width. */
      text_width = font_get_text_width_x2(p_button->psz_label);
      dx = (p_widget->width - text_width)/2;
      dy = (p_widget->height - 32)/2;

      /* Draw text. */
      widget_draw_text_x2(
        p_widget,
        dx,
        dy,
        p_button->psz_label,
        BUTTON_STYLE_TEXT
      );
    }
  }
}

void widget_button_event_handler(widget_t *p_widget, widget_event_t event)
{
  widget_button_t *p_button = (widget_button_t *)p_widget->p_user_data;

  printf("[button] got event %d\n", event);

  if (p_button != NULL)
  {
    switch(event)
    {
      case WE_PRESS:
        {
          p_button->state = BUTTON_PRESSED;
        }
        break;

      case WE_RELEASE:
        {
          p_button->state = BUTTON_RELEASED;
        }
        break;

      case WE_TAP:
        {
          if (p_button->pfn_tap_handler != NULL)
            p_button->pfn_tap_handler(p_widget);
        }
        break;
    }
  }
}

void widget_button_init(
  widget_button_t *p_widget_button,
  tile_t *p_tile,
  int x,
  int y,
  int width,
  int height,
  char *psz_label
)
{
  /* Initialize the underlying widget. */
  widget_init(&p_widget_button->widget, p_tile, x, y, width, height);

  /* Set properties. */
  p_widget_button->state = BUTTON_RELEASED;
  p_widget_button->psz_label = psz_label;

  /* Set user callbacks. */
  p_widget_button->pfn_tap_handler = NULL;

  /* Set user data. */
  widget_set_userdata(&p_widget_button->widget, (void *)p_widget_button);

  /* Set drawing function. */
  widget_set_drawfunc(&p_widget_button->widget, widget_button_drawfunc);

  /* Set default event handler. */
  widget_set_eventhandler(&p_widget_button->widget, widget_button_event_handler);

}


void widget_button_set_handler(widget_button_t *p_widget_button, FTapHandler pfn_handler)
{
  p_widget_button->pfn_tap_handler = pfn_handler;
}
