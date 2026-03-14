/*
 * Simple Hello World
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"

/* Littlevgl specific */
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#include "lvgl_helpers.h"
#include "lvgl_touch/ft6x36.h"

/*********************
 *      DEFINES
 *********************/
#define TAG "helloworld"
#define LV_TICK_PERIOD_MS 1

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void guiTask(void *pvParameter);
static void create_helloworld_application(void);


/****** MY DEFS *****/
typedef struct {
    lv_obj_t *btn;
    char *name;
    int count;
} mybutton_t;

static mybutton_t mybuttons[4];

/**********************
 *   APPLICATION MAIN
 **********************/
void app_main() {

    /* If you want to use a task to create the graphic, you NEED to create a Pinned task
     * Otherwise there can be problem such as memory corruption and so on.
     * NOTE: When not using Wi-Fi nor Bluetooth you can pin the guiTask to core 0 */
    xTaskCreatePinnedToCore(guiTask, "gui", 4096*2, NULL, 0, NULL, 1);
}

/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t xGuiSemaphore;

static void guiTask(void *pvParameter) {

    (void) pvParameter;
    xGuiSemaphore = xSemaphoreCreateMutex();

    lv_init();

    /* Initialize SPI or I2C bus used by the drivers */
    lvgl_driver_init();

    lv_color_t* buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 != NULL);

    /* Use double buffered when not working with monochrome displays */
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    lv_color_t* buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2 != NULL);
#else
    static lv_color_t *buf2 = NULL;
#endif

    static lv_disp_buf_t disp_buf;

    uint32_t size_in_px = DISP_BUF_SIZE;

#if defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_IL3820         \
    || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_JD79653A    \
    || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_UC8151D     \
    || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_SSD1306

    /* Actual size in pixels, not bytes. */
    size_in_px *= 8;
#endif

    /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_buf_init(&disp_buf, buf1, buf2, size_in_px);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = disp_driver_flush;

#if defined CONFIG_DISPLAY_ORIENTATION_PORTRAIT || defined CONFIG_DISPLAY_ORIENTATION_PORTRAIT_INVERTED
    disp_drv.rotated = 1;
#endif

    /* When using a monochrome display we need to register the callbacks:
     * - rounder_cb
     * - set_px_cb */
#ifdef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    disp_drv.rounder_cb = disp_driver_rounder;
    disp_drv.set_px_cb = disp_driver_set_px;
#endif

    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    /* Register an input device when enabled on the menuconfig */
#if CONFIG_LV_TOUCH_CONTROLLER != TOUCH_CONTROLLER_NONE
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.read_cb = touch_driver_read;
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&indev_drv);
#endif

    /* Create and start a periodic timer interrupt to call lv_tick_inc */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    /* Create the demo application */
    create_helloworld_application();

    while (1) {
        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
       }
    }

    /* A task should NEVER return */
    free(buf1);
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    free(buf2);
#endif
    vTaskDelete(NULL);
}

static const char *corner_names[4] = {
    "top-left", "top-right", "bottom-left", "bottom-right"
};

/* static void corner_btn_cb(lv_obj_t *btn, lv_event_t event) */
/* { */
/*     if (event != LV_EVENT_PRESSED) return; */
/*     for (int i = 0; i < 4; i++) { */
/*         lv_indev_t *indev = lv_indev_get_act(); */
/*         lv_point_t pt; */
/*         lv_indev_get_point(indev, &pt); */

/*         printf("HIT \"%s\"  raw(%d,%d)  mapped(%d,%d)  hitbox x=%d y=%d w=%d h=%d\n", */
/*             corner_names[i], */
/*             ft6x36_last_raw_x, ft6x36_last_raw_y, */
/*             pt.x, pt.y, */
/*             lv_obj_get_x(btn), lv_obj_get_y(btn), */
/*             lv_obj_get_width(btn), lv_obj_get_height(btn)); */
/*        break; */
/*     } */
/* } */

static void btn_event_cb(lv_obj_t * btn, lv_event_t event)
{
    if(event == LV_EVENT_CLICKED) {
        mybutton_t *btn_data = (mybutton_t *) lv_obj_get_user_data(btn);
        btn_data->count++;

        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);

        printf("HIT \"%s\"  raw(%d,%d)  mapped(%d,%d)",
               btn_data->name, ft6x36_last_raw_x, ft6x36_last_raw_y, pt.x, pt.y);

        /*Get the first child of the button which is the label and change its text*/
        lv_obj_t * label = lv_obj_get_child(btn, NULL);
        lv_label_set_text_fmt(label, "Button: %d", btn_data->count);
    }
}


static void screen_event_cb(lv_obj_t *scr, lv_event_t event) {
    if (event==LV_EVENT_CLICKED) {

        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);

        lv_obj_t *btn = lv_btn_create(scr, NULL);
        lv_obj_set_size(btn, 10, 10);

        lv_obj_set_x(btn, pt.x);
        lv_obj_set_y(btn, pt.y);
    }
}

static void create_helloworld_application(void)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_set_event_cb(scr, screen_event_cb);



    static const lv_align_t aligns[4] = {
        LV_ALIGN_IN_TOP_LEFT,
        LV_ALIGN_IN_TOP_RIGHT,
        LV_ALIGN_IN_BOTTOM_LEFT,
        LV_ALIGN_IN_BOTTOM_RIGHT,
    };
    static const lv_point_t offsets[4] = {
        {5, 5}, {-5, 5}, {5, -5}, {-5, -5}
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(scr, NULL);
        lv_obj_set_size(btn, 20, 20);
        lv_obj_align(btn, NULL, aligns[i], offsets[i].x, offsets[i].y);
        lv_obj_set_event_cb(btn, btn_event_cb);
        lv_obj_set_user_data(btn, &mybuttons[i]);
        mybuttons[i].name = corner_names[i];
        mybuttons[i].count = 0;
        mybuttons[i].btn = btn;

        lv_obj_t *lbl = lv_label_create(btn, NULL);
        lv_label_set_text(lbl, corner_names[i]);
        lv_obj_set_event_cb(btn, btn_event_cb);
        printf("btn \"%s\" hitbox: x=%d y=%d w=%d h=%d\n",
            corner_names[i],
            lv_obj_get_x(btn), lv_obj_get_y(btn),
            lv_obj_get_width(btn), lv_obj_get_height(btn));
    }


}

static void lv_tick_task(void *arg) {
    (void) arg;

    lv_tick_inc(LV_TICK_PERIOD_MS);
}
