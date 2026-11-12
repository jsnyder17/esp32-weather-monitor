#include <string.h>
#include <shiftregister_gpio.h>
#include <semaphore.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "bus/dgx_spi_esp32.h"
#include "drivers/ili9341.h"
#include "dgx_bits.h"
#include "dgx_colors.h"
#include "dgx_draw.h"
#include "dgx_font.h"
#include "fonts/DroidSansRegular9.h"
#include "json_parser.h"
#include "main.h"
#include "drivers/vscreen.h"
#include "iot_button.h"
#include "button_gpio.h"

//*********************************************************************************************************************

uint16_t fast_blend(uint8_t alpha, uint32_t fgc, uint32_t bgc) {
  // Split out and blend 5-bit red and blue channels
  uint32_t rxb = bgc & 0xF81F;
  rxb += ((fgc & 0xF81F) - rxb) * (alpha >> 2) >> 6;
  // Split out and blend 6-bit green channel
  uint32_t xgx = bgc & 0x07E0;
  xgx += ((fgc & 0x07E0) - xgx) * alpha >> 8;
  // Recombine channels
  return (rxb & 0xF81F) | (xgx & 0x07E0);
}

void fill_rect_v_gradient(int16_t x, int16_t y, int w, int h, uint32_t color_1, uint32_t color_2) {
    float delta = -255.0 / h;
    float alpha = 255.0;
    uint32_t color = color_1;

    while (h--) {
        dgx_draw_line(tft, x, y, x + w, y, color);
        alpha += delta;
        color = fast_blend((uint8_t) alpha, color_1, color_2);
        y++;
    }
}

char* string_to_lower(const char *str) {
	size_t len = strlen(str) + 1;
	char *lower_str = (char*) malloc(len);
	memset(lower_str, 0, len);

	int i;
	for (i = 0; i < len; i++) {
		lower_str[i] = tolower(str[i]);
	}
	
	return lower_str;
}

bool compare_strings(const char *str_1, const char *str_2, int usr_comp_size) {
	char *lower_str_1 = string_to_lower(str_1);
	char *lower_str_2 = string_to_lower(str_2);

	// strncmp will prevent a buffer overflow, but we also want to make 
	// sure we compare the entire string and not just part of it, so 
	// use the longest string as the comparison size.
    bool result;
    if (usr_comp_size == -1) {
        size_t size_1 = strlen(lower_str_1);
	    size_t size_2 = strlen(lower_str_2);
	    size_t comp_size = size_1 > size_2 ? size_1 : size_2;
	    result = strncmp(lower_str_1, lower_str_2, comp_size) == 0;
    } else {
        result = strncmp(lower_str_1, lower_str_2, usr_comp_size) == 0;
    }
	
	free(lower_str_1);
	free(lower_str_2);

	return result;
}

bool string_contains(const char *str_1, const char *str_2) {
	char *lower_str_1 = string_to_lower(str_1);
	char *lower_str_2 = string_to_lower(str_2);

	char *result = strstr(lower_str_1, lower_str_2);

	free(lower_str_1);
	free(lower_str_2);

	return result != NULL;
}

//*********************************************************************************************************************

struct MenuNode* find_grandparent_node(struct MenuNode *node, struct MenuNode *grandparent_node) {
    if (node == NULL) return NULL;
	if (node == curr_parent_node) return grandparent_node;
	
	// Recurse through the path of the first child.
	struct MenuNode *node_1 = find_grandparent_node(node->first_child, node);
	if (node_1 != NULL) return node_1;

	struct MenuNode *node_2 = find_grandparent_node(node->next_sibling, grandparent_node);
	if (node_2 != NULL) return node_2;

	return NULL;
}

void exit_menu() {
    curr_parent_node = root_menu_node;
    curr_section_lead_node = root_menu_node->first_child;
    curr_menu_node = curr_section_lead_node;
    curr_menu_pos = 0;
    curr_state = STATE_CC;
    refresh_display = true;
}

void perform_menu_action() {
	// Back (go back to the previous menu (parent node's parent))
	if (compare_strings(MENU_OPTION_BACK, curr_menu_node->data, 4)) {
		curr_parent_node = find_grandparent_node(root_menu_node, NULL);
		if (curr_parent_node != NULL) {
			curr_section_lead_node = curr_parent_node->first_child;
			curr_menu_node = curr_section_lead_node;
			curr_menu_pos = 0;
            refresh_display = true;
		} else {
            exit_menu();
        }
    } else if (compare_strings(MENU_OPTION_SYSTEM_INFO, curr_menu_node->data, -1)) {
        curr_state = STATE_SI;
        refresh_display = true;
	} else if (compare_strings(MENU_OPTION_CLEAR_ERROR_CODE, curr_menu_node->data, -1)) {
        error_bits = 0b00000000;
    } else if (compare_strings(MENU_OPTION_CLEAR, curr_menu_node->data, -1)) {
        memset(cc_desc, 0, MAX_DESC_CHARS);
        memcpy(cc_desc, CC_CLEAR, MAX_DESC_CHARS);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_CLOUDY, curr_menu_node->data, -1)) {
        memset(cc_desc, 0, MAX_DESC_CHARS);
        memcpy(cc_desc, MENU_OPTION_CLOUDY, MAX_DESC_CHARS);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_PARTLY_CLOUDY, curr_menu_node->data, -1)) {
        memset(cc_desc, 0, MAX_DESC_CHARS);
        memcpy(cc_desc, MENU_OPTION_PARTLY_CLOUDY, MAX_DESC_CHARS);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_SCATTERED_SHOWERS, curr_menu_node->data, -1)) {
        memset(cc_desc, 0, MAX_DESC_CHARS);
        memcpy(cc_desc, MENU_OPTION_SCATTERED_SHOWERS, MAX_DESC_CHARS);
        exit_menu();
    } else if (compare_strings(MENU_OPTION_THUNDERSTORM, curr_menu_node->data, -1)) {
        memset(cc_desc, 0, MAX_DESC_CHARS);
        memcpy(cc_desc, MENU_OPTION_THUNDERSTORM, MAX_DESC_CHARS);
        exit_menu();
    }
}

struct MenuNode* create_menu_node(const char *data) {
	struct MenuNode *new_node = (struct MenuNode*) malloc(sizeof(struct MenuNode));
	memset(new_node, 0, sizeof(struct MenuNode));
	memcpy(new_node->data, data, MAX_MENU_NODE_DATA_CHARS);

	return new_node;
}

//*********************************************************************************************************************

esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        gpio_set_level(PIN_INC_DATA_LED, 1);
        int copy = evt->data_len;
        if (http_response_buffer_len + copy >= HTTP_GET_RESPONSE_BUFFER_SIZE) {
            copy = HTTP_GET_RESPONSE_BUFFER_SIZE - http_response_buffer_len - 1;
        }
        memcpy(http_response_buffer + http_response_buffer_len, evt->data, copy);
        http_response_buffer_len += copy;
        http_response_buffer[http_response_buffer_len] = '\0';
        gpio_set_level(PIN_INC_DATA_LED, 0);
    }
    return ESP_OK;
}

//*********************************************************************************************************************

void draw_header_rect() {
    fill_rect_v_gradient(0, 10, 210, 40, DGX_COPPER(dgx_rgb_to_16), DGX_NAVY(dgx_rgb_to_16));
}

void draw_string(const char *str, int16_t x, int16_t y, uint32_t color, int scale) {
    dgx_font_string_utf8_screen(tft, x + SHADOW_OFFSET, y + SHADOW_OFFSET, str, DGX_BLACK(dgx_rgb_to_16), 
        DgxOutputNormal, scale, DroidSansRegular9(), NULL, NULL);
    dgx_font_string_utf8_screen(tft, x, y, str, color, DgxOutputNormal, scale, DroidSansRegular9(), NULL, NULL);
}

void draw_header_string(const char *str) {
    draw_string(str, 10, 35, DGX_YELLOW(dgx_rgb_to_16), 2);
}

void draw_bitmap(const uint16_t *bitmap, int size, int x, int y, uint32_t scale) {
    uint8_t image_be[size * size * 2];

    for (int i = 0; i < size * size; ++i) {
        uint16_t pixel = bitmap[i];
        image_be[2 * i] = pixel >> 8;
        image_be[2 * i + 1] = pixel & 0xFF;
    }

    dgx_screen_t *image = dgx_vscreen_from_array(image_be, size, size, 16, DgxScreenRGB);
    dgx_vscreen_to_screen(tft, x, y, image, DGX_VSCREEN_SCALE(scale) | DGX_VSCREEN_TRANSPARENT);
}

void draw_cloud(int x, int y, uint32_t color) {
    dgx_solid_circle(tft, (x + SHADOW_OFFSET), (y + SHADOW_OFFSET), 15, DGX_BLACK(dgx_rgb_to_16));
    dgx_solid_circle(tft, (x + 10 + SHADOW_OFFSET), ((y - 10) + SHADOW_OFFSET), 15, DGX_BLACK(dgx_rgb_to_16));
    dgx_solid_circle(tft, (x + 20 + SHADOW_OFFSET), (y + 5 + SHADOW_OFFSET), 15, DGX_BLACK(dgx_rgb_to_16));
    
    dgx_solid_circle(tft, x, y, 15, color);
    dgx_solid_circle(tft, (x + 10), (y - 10), 15, color);
    dgx_solid_circle(tft, (x + 20), (y + 5), 15, color);
}

void draw_sun(int x, int y, int scale) {
    dgx_solid_circle(tft, x + SHADOW_OFFSET, y + SHADOW_OFFSET, 1 * scale, DGX_BLACK(dgx_rgb_to_16));
    dgx_solid_circle(tft, x, y, scale, DGX_YELLOW(dgx_rgb_to_16));
}

void draw_condition_clear() {
    draw_bitmap(sun_bitmap, 32, 70, 85, 3);
}

void draw_condition_cloudy() {
    draw_bitmap(cloud_bitmap, 32, 50, 110, 2);
    draw_bitmap(cloud_bitmap, 32, 80, 90, 2);
    draw_bitmap(cloud_bitmap, 32, 110, 105, 2);
}

void draw_condition_partly_cloudy() {
    draw_bitmap(sun_bitmap, 32, 70, 85, 3);
    draw_bitmap(cloud_bitmap, 32, 75, 85, 3);
}

void draw_condition_rain() {
    draw_bitmap(rain_bitmap, 32, 70, 85, 3);
}

void draw_condition_storm() {
    draw_bitmap(storm_bitmap, 32, 70, 85, 3);
}

void draw_condition_unknown() {
    draw_bitmap(unknown_bitmap, 32, 70, 85, 3);
}

void clear_screen() {
    fill_rect_v_gradient(0, 0, tft->width, tft->height, DGX_NAVY(dgx_rgb_to_16), DGX_COPPER(dgx_rgb_to_16));
}

//*********************************************************************************************************************

void reset_response_buffer(char *response_buf, int *response_len) {
    memset(response_buf, 0, HTTP_GET_RESPONSE_BUFFER_SIZE);
    *response_len = 0;
}

bool get_bit_val_at_pos(uint8_t num, uint8_t bit_pos) {
    // Cast the first operation result as uint8_t because C converts to int during shift operations.
    return ((uint8_t) num << bit_pos) >> 7; 
}

//*********************************************************************************************************************

void set_int_temperature_val(int *temperature_val) {
    if (DEG_F) {
        *temperature_val = (*temperature_val * 1.8) + 32;
    }
    ESP_LOGI(TAG_INFO, "Int: %i", *temperature_val);
}

void set_float_temperature_val(float *temperature_val) {
    if (DEG_F) {
        *temperature_val = (*temperature_val * 1.8) + 32;
    }
    ESP_LOGI(TAG_INFO, "Float: %.f", *temperature_val);
}

bool update_time(void) {
    ESP_LOGI(TAG_INFO, "Getting time ...");

    // Reset response buffer;
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);

    // Configure HTTP client
    config.url = TIME_API_URL;
    config.event_handler = http_event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    // Initialize HTTP client and perform the HTTP GET request
    client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_TIME_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
        return false;
    }

    jparse_ctx_t jparse_ctx;
    if (json_parse_start(&jparse_ctx, http_response_buffer, http_response_buffer_len) != OS_SUCCESS) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Error starting time JSON parsing.");
        return false;
    }
    if (!(json_obj_get_string(&jparse_ctx, "date", date, DATE_TIME_BUFFER_SIZE) == OS_SUCCESS &&
            json_obj_get_string(&jparse_ctx, "time", time_, DATE_TIME_BUFFER_SIZE) == OS_SUCCESS)) {
            error_bits = ERROR_P_JSON;
            ESP_LOGE(TAG_ERROR, "Error parsing time JSON.");
            return false;
    }
    if (json_obj_get_int(&jparse_ctx, "day", &curr_day) != OS_SUCCESS) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing time JSON.");
        return false;
    }
    if (json_parse_end(&jparse_ctx) != OS_SUCCESS) {
        error_bits = ERROR_EP_JSON;
        ESP_LOGE(TAG_ERROR, "Error ending time JSON parsing.");
        return false;
    }

    ESP_LOGI(TAG_INFO, "Got time.");
    
    // Cleanup HTTP client
    esp_http_client_cleanup(client);

    return true;
}

bool update_curr_conditions() {
    ESP_LOGI(TAG_INFO, "Getting current conditions ...");

    // Set the temperature char.
    temperature_char = DEG_F ? 'F' : 'C';

    // Configure the HTTP client.
	size_t request_url_size = strlen(API_HOST) + strlen(NWS_API_POINTS_URL_PART) + strlen(LATITUDE) 
        + strlen(LAT_LONG_DELIM) + strlen(LONGITUDE) + 1;
    if (request_url_size > MAX_REQUEST_URL_SIZE) {
        error_bits = ERROR_REQUEST_URL_LIMIT;
        ESP_LOGE(TAG_ERROR, "The request URL for init exceeds the allowed number of bytes.");
    }

	char request_url[MAX_REQUEST_URL_SIZE];
	memset(request_url, 0, MAX_REQUEST_URL_SIZE);
	strcat(request_url, API_HOST);
	strcat(request_url, NWS_API_POINTS_URL_PART);
	strcat(request_url, LATITUDE);
	strcat(request_url, LAT_LONG_DELIM);
	strcat(request_url, LONGITUDE);

    // Configure HTTP client
    config.url = request_url;
    config.event_handler = http_event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    // Reset response buffer;
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);

    // Initialize HTTP client and perform the HTTP GET request
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);

    jparse_ctx_t jparse_ctx;
    int ret;
    if ((ret = json_parse_start(&jparse_ctx, http_response_buffer, http_response_buffer_len)) != OS_SUCCESS) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "json_parse_start failed (ret=%d, len=%d) 1", ret, http_response_buffer_len);
    }

    char ob_stations_url_raw[MAX_OB_STN_SIZE];
    memset(ob_stations_url_raw, 0, MAX_OB_STN_SIZE);
    bool ok = json_obj_get_object(&jparse_ctx, JSON_PROPERTIES) == OS_SUCCESS && 
        json_obj_get_string(&jparse_ctx, JSON_OBSERVATION_STATIONS, ob_stations_url_raw, MAX_OB_STN_SIZE) == OS_SUCCESS;
    if (!ok) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing initial JSON.");
    }
    if (json_parse_end(&jparse_ctx) != OS_SUCCESS) {
        error_bits = ERROR_EP_JSON;
        ESP_LOGE(TAG_ERROR, "Error ending initial JSON parsing.");
    }

	// Make another request to actually get the current observations now that we have the URL.
	size_t ob_stations_url_size = MAX_OB_STN_SIZE + strlen(PARAM_START) + strlen(NWS_API_URL_PART_LIMIT_PARAM) + 
        strlen(NWS_API_ITEM_LIMIT) + strlen(NWS_API_ITEM_LIMIT) + 1;
    if (ob_stations_url_size > MAX_REQUEST_URL_SIZE) {
        error_bits = ERROR_REQUEST_URL_LIMIT;
        ESP_LOGE(TAG_ERROR, "The request URL for ob stations exceeds the allowed number of bytes.");
    }

	memset(request_url, 0, MAX_REQUEST_URL_SIZE);
	strcat(request_url, ob_stations_url_raw);
	strcat(request_url, PARAM_START);
	strcat(request_url, NWS_API_URL_PART_LIMIT_PARAM);
	strcat(request_url, NWS_API_ITEM_LIMIT);

    // Reset response buffer;
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);
    
    // Initialize HTTP client and perform the HTTP GET request
    client = esp_http_client_init(&config);
    err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);

    if (json_parse_start(&jparse_ctx, http_response_buffer, http_response_buffer_len) != OS_SUCCESS) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Error starting ob station JSON parsing.");
    }
    int num_elem = 1;
    char stations_request_str_raw[MAX_DESC_CHARS];
    memset(stations_request_str_raw, 0, MAX_DESC_CHARS);
    ok = json_obj_get_array(&jparse_ctx, JSON_OBSERVATION_STATIONS, &num_elem) == OS_SUCCESS && 
        json_arr_get_string(&jparse_ctx, 0, stations_request_str_raw, MAX_DESC_CHARS) == OS_SUCCESS;
    if (!ok){ 
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing ob station JSON.");
    }
    if (json_parse_end(&jparse_ctx) != OS_SUCCESS) {
        error_bits = ERROR_EP_JSON;
        ESP_LOGE(TAG_ERROR, "Error ending ob station JSON parsing.");
    }

    // Finally, make a request to get the latest observations from the station.
	size_t stations_request_url_size = strlen(stations_request_str_raw) 
        + strlen(NWS_API_OBSERVATIONS_LATEST_URL_PART) + 1;
    if (stations_request_url_size > MAX_REQUEST_URL_SIZE) {
        error_bits = ERROR_REQUEST_URL_LIMIT;
        ESP_LOGE(TAG_ERROR, "The request URL for stations exceeds the allowed number of bytes.");
    }

	memset(request_url, 0, MAX_REQUEST_URL_SIZE);
	strcat(request_url, stations_request_str_raw);
	strcat(request_url, NWS_API_OBSERVATIONS_LATEST_URL_PART);

    // Reset response buffer;
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);

    // Initialize HTTP client and perform the HTTP GET request
    client = esp_http_client_init(&config);
    err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);

    // Get temperature data
    if (json_parse_start(&jparse_ctx, http_response_buffer, http_response_buffer_len) != OS_SUCCESS) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "Error starting station JSON parsing.");
    }
    
    bool bool_1 = json_obj_get_object(&jparse_ctx, JSON_PROPERTIES) == OS_SUCCESS;
    if (!bool_1) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_1.");
    }
    bool bool_2 = json_obj_get_string(&jparse_ctx, JSON_TEXT_DESCRIPTION, cc_desc, MAX_DESC_CHARS) == OS_SUCCESS;
    if (!bool_2) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_2.");
    }    
    bool bool_3 = json_obj_get_object(&jparse_ctx, JSON_TEMPERATURE) == OS_SUCCESS;
    if (!bool_3) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_3.");
    }    
    int temp_temperature;
    bool bool_4 = json_obj_get_int(&jparse_ctx, JSON_VALUE, &temp_temperature) == OS_SUCCESS;
    if (!bool_4) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_4.");
    } else {   
        temperature = temp_temperature;
        set_int_temperature_val(&temperature); 
    }

    bool bool_5 = json_obj_leave_object(&jparse_ctx) == OS_SUCCESS;
    if (!bool_5) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_5.");
    }    
    bool bool_6 = json_obj_get_object(&jparse_ctx, JSON_WIND_SPEED) == OS_SUCCESS;
    if (!bool_6) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_6.");
    }    
    bool bool_7 = json_obj_get_float(&jparse_ctx, JSON_VALUE, &wind_speed) == OS_SUCCESS;
    if (!bool_7) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_7.");
    }    
    set_float_temperature_val(&dew_point);
    bool bool_8 = json_obj_leave_object(&jparse_ctx) == OS_SUCCESS;
    if (!bool_8) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_8.");
    }    
    bool bool_9 = json_obj_get_object(&jparse_ctx, JSON_WIND_DIR) == OS_SUCCESS;
    if (!bool_9) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_9.");
    }    
    int temp_wind_direction;
    bool bool_10 = json_obj_get_int(&jparse_ctx, JSON_VALUE, &temp_wind_direction) == OS_SUCCESS;
    if (!bool_10) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_10.");
    } else {
        wind_direction = temp_wind_direction;
    }
    bool bool_11 = json_obj_leave_object(&jparse_ctx) == OS_SUCCESS;
    if (!bool_11) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_11.");
    }
    bool bool_12 = json_obj_get_object(&jparse_ctx, JSON_DEWPOINT) == OS_SUCCESS;
    if (!bool_12) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_12.");
    }
    float temp_dew_point;
    bool bool_13 = json_obj_get_float(&jparse_ctx, JSON_VALUE, &temp_dew_point) == OS_SUCCESS;
    if (!bool_13) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error bool_13.");
    } else {
        dew_point = temp_dew_point;
        set_float_temperature_val(&dew_point);
    }

    if (json_parse_end(&jparse_ctx) != OS_SUCCESS) {
        error_bits = ERROR_EP_JSON;
        ESP_LOGE(TAG_ERROR, "Error ending station JSON parsing.");
    }

    // Calculate the wind direction string
    memset(wind_dir, 0, MAX_WIND_DIR_SIZE);

    if (wind_direction <= 15.0f || wind_direction > 345.0f) {           // North
        memcpy(wind_dir, WIND_DIR_N, 3);
    } else if (wind_direction > 15.0f && wind_direction <= 75.0f) {     // North-west
        memcpy(wind_dir, WIND_DIR_NW, 3);
    } else if (wind_direction > 75.0f && wind_direction <= 105.0f) {    // West
        memcpy(wind_dir, WIND_DIR_W, 3);
    } else if (wind_direction > 105.0f && wind_direction <= 165.0f) {   // South-west
        memcpy(wind_dir, WIND_DIR_SW, 3);
    } else if (wind_direction > 165.0f && wind_direction <= 195.0f) {   // South
        memcpy(wind_dir, WIND_DIR_S, 3);
    } else if (wind_direction > 195.0f && wind_direction <= 255.0f) {   // South-east
        memcpy(wind_dir, WIND_DIR_SE, 3);
    } else if (wind_direction > 255.0f && wind_direction <= 285.0f) {   // East
        memcpy(wind_dir, WIND_DIR_E, 3);
    } else {                                                            // North-east
        memcpy(wind_dir, WIND_DIR_NE, 3);
    }

    ESP_LOGI(TAG_INFO, "Got current conditions");

    return true;
}

bool update_forecast() {
    ESP_LOGI(TAG_INFO, "Getting forecast ...");

    // Configure the HTTP client.
	size_t request_url_size = strlen(API_HOST) + strlen(NWS_API_POINTS_URL_PART) + strlen(LATITUDE) 
        + strlen(LAT_LONG_DELIM) + strlen(LONGITUDE) + 1;
    if (request_url_size > MAX_REQUEST_URL_SIZE) {
        error_bits = ERROR_REQUEST_URL_LIMIT;
        ESP_LOGE(TAG_ERROR, "The request URL for init exceeds the allowed number of bytes.");
        return false;
    }

	char request_url[MAX_REQUEST_URL_SIZE];
	memset(request_url, 0, MAX_REQUEST_URL_SIZE);
	strcat(request_url, API_HOST);
	strcat(request_url, NWS_API_POINTS_URL_PART);
	strcat(request_url, LATITUDE);
	strcat(request_url, LAT_LONG_DELIM);
	strcat(request_url, LONGITUDE);

    // Reset response buffer;
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);

    // Configure the HTTP client.
    config.url = request_url;
    config.event_handler = http_event_handler;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    // Initialize HTTP client and perform the HTTP GET request
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_http_client_cleanup(client);

    jparse_ctx_t jparse_ctx;
    int ret;
    if ((ret = json_parse_start(&jparse_ctx, http_response_buffer, http_response_buffer_len)) != OS_SUCCESS) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "json_parse_start failed (ret=%d, len=%d) 1", ret, http_response_buffer_len);
        return false;
    }

    // Get the forecasts URL
    if (json_obj_get_object(&jparse_ctx, JSON_PROPERTIES) != OS_SUCCESS) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing forecast JSON.");
        return false;
    }

    memset(request_url, 0, MAX_REQUEST_URL_SIZE);
    if (json_obj_get_string(&jparse_ctx, JSON_FORECAST, request_url, MAX_REQUEST_URL_SIZE) != OS_SUCCESS) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing forecast JSON \"forecast\".");
        return false;
    }

    // Reset response buffer.
    reset_response_buffer(http_response_buffer, &http_response_buffer_len);
    config.url = request_url;

    // Initialize HTTP client and perform the HTTP GET request
    client = esp_http_client_init(&config);
    err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        error_bits = ERROR_NWS_API_GET;
        ESP_LOGE(TAG_HTTP_GET, "HTTP request failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_http_client_cleanup(client);

    if ((ret = json_parse_start(&jparse_ctx, http_response_buffer, http_response_buffer_len)) != OS_SUCCESS) {
        error_bits = ERROR_SP_JSON;
        ESP_LOGE(TAG_ERROR, "json_parse_start failed (ret=%d, len=%d) 1", ret, http_response_buffer_len);
        return false;
    }

    // Iterate through the "periods" until we get the first "isDayTime": true object.
    if (json_obj_get_object(&jparse_ctx, JSON_PROPERTIES) != OS_SUCCESS) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing forecast JSON \"properties\".");
        return false;
    }

    int num_elements;
    if (json_obj_get_array(&jparse_ctx, JSON_PERIODS, &num_elements) != OS_SUCCESS) {
        error_bits = ERROR_P_JSON;
        ESP_LOGE(TAG_ERROR, "Error parsing forecast JSON \"periods\".");
        return false;
    }
    
    uint32_t i;
    bool is_daytime = false;
    for (i = 0; i < num_elements; i++) {
        if (json_arr_get_object(&jparse_ctx, i) != OS_SUCCESS) {
            error_bits = ERROR_P_JSON;
            ESP_LOGI(TAG_ERROR, "%s %i", "Error parsing forecast JSON \"periods\" object.");
            return false;
        }
        if (json_obj_get_bool(&jparse_ctx, JSON_IS_DAYTIME, &is_daytime) != OS_SUCCESS) {
            error_bits = ERROR_P_JSON;
            ESP_LOGE(TAG_ERROR, "Error parsing forecast JSON \"isDaytime\".");
            return false;
        }
        if (!is_daytime) {
            if (json_obj_leave_object(&jparse_ctx) != OS_SUCCESS) {
                error_bits = ERROR_P_JSON;
                ESP_LOGE(TAG_ERROR, "Error parsing forecast JSON leaving loop object.");
                return false;
            }
            continue;
        }
        break;
    }

    // We found a daytime object, so grab the temperature from it.
    if (is_daytime) {
        int temp_high;
        if (json_obj_get_int(&jparse_ctx, JSON_TEMPERATURE, &temp_high) != OS_SUCCESS) {
            error_bits = ERROR_P_JSON;
            ESP_LOGE(TAG_ERROR, "Error parsing forecast JSON \"temperature\".");
            return false;
        } else {
            high_temperature = temp_high;
        }
    }

    return is_daytime;
}

bool update_seven_seg() {
    // Get the bits from the current error code.
    bool error_arr[4];
    uint8_t i = 0;
    for (i = 0; i < 4; i++) {
        error_arr[i] = get_bit_val_at_pos(error_bits, 4 + i);
    }

    // We need to output the current error code serially to the register.
    uint8_t register_val = 0b00000000;
	
	uint8_t seven_seg_a = (!error_arr[1] && !error_arr[3]) || (!error_arr[0] && error_arr[2]) 
		|| (error_arr[1] && error_arr[2]) || (error_arr[0] && !error_arr[3]) 
		|| (!error_arr[0] && error_arr[1] && error_arr[3]) || (error_arr[0] && !error_arr[1] && !error_arr[2]);
	uint8_t seven_seg_b = (!error_arr[0] && !error_arr[1]) || (!error_arr[1] && !error_arr[3])
		|| (!error_arr[0] && !error_arr[2] && !error_arr[3]) || (!error_arr[0] && error_arr[2] && error_arr[3]) 
		|| (error_arr[0] && !error_arr[2] && error_arr[3]);
	uint8_t seven_seg_c = (!error_arr[0] && !error_arr[2]) || (!error_arr[0] && error_arr[3]) 
		|| (!error_arr[2] && error_arr[3]) || (!error_arr[0] && error_arr[1]) || (error_arr[0] && !error_arr[1]);
	uint8_t seven_seg_d = (error_arr[0] && !error_arr[2]) 
		|| (!error_arr[0] && !error_arr[1] && !error_arr[3]) || (!error_arr[1] && error_arr[2] && error_arr[3])
		|| (error_arr[1] && !error_arr[2] && error_arr[3]) || (error_arr[1] && error_arr[2] && !error_arr[3]);
	uint8_t seven_seg_e = (!error_arr[1] && !error_arr[3]) || (error_arr[2] && !error_arr[3]) 
		|| (error_arr[0] && error_arr[2]) || (error_arr[0] && error_arr[1]);
	uint8_t seven_seg_f =  (!error_arr[2] && !error_arr[3]) || (error_arr[1] && !error_arr[3]) 
		|| (error_arr[0] && !error_arr[1]) || (error_arr[0] && error_arr[2]) 
		|| (!error_arr[0] && error_arr[1] && !error_arr[2]);
	uint8_t seven_seg_g = (!error_arr[1] && error_arr[2]) || (error_arr[2] && !error_arr[3]) 
		|| (error_arr[0] && !error_arr[1]) || (error_arr[0] && error_arr[3]) 
		|| (!error_arr[0] && error_arr[1] && !error_arr[2]);
	
	register_val |= seven_seg_g;
	register_val |= (seven_seg_f << 1);
	register_val |= (seven_seg_a << 2);
	register_val |= (seven_seg_b << 3);
	register_val |= (seven_seg_e << 4);
	register_val |= (seven_seg_d << 5);
	register_val |= (seven_seg_c << 6);

    uint8_t reg_vals[1] = {register_val};

    ESP_ERROR_CHECK(shiftregister_gpio_transfer(&reg_config, reg_vals, sizeof(reg_vals)));

    return true;
}

void update_menu(bool menu, bool menu_up, bool menu_down) {
	// Move the cursor and check if we need to display a new section of the menu.
	if (menu_up && (curr_menu_node->prev_sibling != NULL)) {
		curr_menu_node = curr_menu_node->prev_sibling;
		curr_menu_pos--;
		if (curr_menu_pos < 0) {
			// Get the node 4 spots behind the current node and start the section with it.
            uint8_t starting_index = MENU_MAX_OPTIONS_DISPLAYED - 1;
			uint8_t i = starting_index;
			struct MenuNode *temp_node = curr_menu_node;
			while (i > 0 && temp_node->prev_sibling != NULL) {
				temp_node = temp_node->prev_sibling;
				i--;
			}				
			curr_section_lead_node = temp_node;
			curr_menu_pos = starting_index;
            ESP_LOGI(TAG_INFO, "curr_section_lead_node: %s", curr_section_lead_node->data);
		}
        refresh_display = true;
	} else if (menu_down && (curr_menu_node->next_sibling != NULL)) {
		curr_menu_node = curr_menu_node->next_sibling;
		curr_menu_pos++;
		if (curr_menu_pos == MENU_MAX_OPTIONS_DISPLAYED) {
			curr_section_lead_node = curr_menu_node;
            curr_menu_pos = 0;
		}
        refresh_display = true;
	} else if (menu) {
		if (curr_menu_node->first_child != NULL) {
			curr_parent_node = curr_menu_node;
			curr_menu_node = curr_menu_node->first_child;
			curr_section_lead_node = curr_menu_node;
			curr_menu_pos = 0;
            refresh_display = true;		
		} else {
			perform_menu_action();
		}
	}
}

//*********************************************************************************************************************

void draw_cc_view() {
    // Clear the screen.
    clear_screen();

    // Draw the header rectangle
    draw_header_rect();

    // Draw the header text
    draw_header_string(HEADER_CURR_CONDITIONS);

    // Draw container rectangle
    dgx_fill_rectangle(tft, 5, 60, 310, 170, DGX_DARKCYAN(dgx_rgb_to_16));
    dgx_fill_rectangle(tft, 7, 62, 306, 166, CC_CONTAINER_COLOR);

    // Write the date and time.
    draw_string(date, 220, 25, DGX_WHITE(dgx_rgb_to_16), 2);
    draw_string(time_, 240, 45, DGX_WHITE(dgx_rgb_to_16), 2);

    // Write the high.
    char high_temp_buff[10];
    memset(high_temp_buff, 0, 10);
    snprintf(high_temp_buff, 10, ": %i %c", high_temperature, temperature_char);
    draw_string("H", 250, 95, DGX_YELLOW(dgx_rgb_to_16), 2);
    draw_string(high_temp_buff, 263, 95, DGX_WHITE(dgx_rgb_to_16), 2);

    // Write the temperature.
    char temp_buff[5];
    memset(temp_buff, 0, 5);
    snprintf(temp_buff, 5, "%i %c", temperature, temperature_char);
    draw_string(temp_buff, 250, 130, DGX_WHITE(dgx_rgb_to_16), 3);

    // Write the wind data.
    uint16_t wind_buff_size = 10 + MAX_WIND_DIR_SIZE;
    char wind_buff[wind_buff_size];
    memset(wind_buff, 0, wind_buff_size);
    snprintf(wind_buff, wind_buff_size, "%s %.1f mph", wind_dir, wind_speed);
    draw_string(wind_buff, 210, 160, DGX_WHITE(dgx_rgb_to_16), 2);

    // Write the dew point
    char dew_point_buff[8];
    memset(dew_point_buff, 0, 8);
    snprintf(dew_point_buff, 8, "%.1f %c", dew_point, temperature_char);
    draw_string(dew_point_buff, 240, 185, DGX_WHITE(dgx_rgb_to_16), 2);
    draw_bitmap(dew_point_bitmap, 16, 225, 172, 1);

    // Write the current description.
    draw_string(cc_desc, 30, 210, DGX_WHITE(dgx_rgb_to_16), 3);

    // Determine what condition to draw based on the description.
    if (string_contains(cc_desc, CC_CLEAR)) {
        draw_condition_clear();
    } else if (string_contains(cc_desc, CC_PARTLY)) {
        draw_condition_partly_cloudy();
    } else if (string_contains(cc_desc, CC_CLOUDY)) {
        draw_condition_cloudy();
    } else if (string_contains(cc_desc, CC_THUNDER)) {
        draw_condition_storm();
    } else if (string_contains(cc_desc, CC_RAIN) || string_contains(cc_desc, CC_SHOWERS)) {
        draw_condition_rain();
    } else {
        draw_condition_unknown();
    }
}

void draw_menu_view() {
    // Always clear the screen before writing the menu
	clear_screen();

    // Draw the header rect.
    draw_header_rect();

	// Display menu header text
    draw_header_string(root_menu_node->data);

	// Show each of the options by traversing through the children of the current parent menu node.
	const char *cursor = " <";
	size_t cursor_size = strlen(cursor);
	size_t option_size = MAX_MENU_NODE_DATA_CHARS + cursor_size;
	char option[option_size];
	memset(option, 0, option_size);

	struct MenuNode *temp_curr_menu_node = curr_section_lead_node;
	uint8_t i = 0;
    uint32_t color;
	while (temp_curr_menu_node != NULL && i < MENU_MAX_OPTIONS_DISPLAYED) {
		strcat(option, temp_curr_menu_node->data);
        
		if (temp_curr_menu_node == curr_menu_node) {
            strcat(option, cursor);
            color = DGX_ORANGE(dgx_rgb_to_16);
        } else {
            color = DGX_WHITE(dgx_rgb_to_16);
        }
        draw_string(option, MENU_OPTIONS_X, 60 + (MENU_OPTIONS_OFFSET * (i + 1)), color, 2);

		memset(option, 0, option_size);
		temp_curr_menu_node = temp_curr_menu_node->next_sibling;
		i++;
	}
}

//*********************************************************************************************************************

void http_get_time(void *arg) {
    while (true) {
        if (xSemaphoreTake(mutex, 0) == pdTRUE) {
            refresh_display = update_time() && curr_state == STATE_CC;
            xSemaphoreGive(mutex);
            // Wait for 60 seconds before the next request
            vTaskDelay(pdMS_TO_TICKS(TIME_REFRESH_TIME_MS)); 
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }   
    }
}

void http_get_curr_conditions(void *arg) {
    while (true) {
        if (xSemaphoreTake(mutex, 0) == pdTRUE) {
            refresh_display = update_curr_conditions() && curr_state == STATE_CC;
            xSemaphoreGive(mutex);
            // Wait for 5 minutes before the next request
            vTaskDelay(pdMS_TO_TICKS(CURR_CONDITIONS_REFRESH_TIME_MS)); 
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void http_get_forecast(void *arg) {
    while (true) {
        if (xSemaphoreTake(mutex, 0) == pdTRUE) {
            if (curr_day != prev_day && update_forecast() && curr_state == STATE_CC) {
                refresh_display = true;
                prev_day = curr_day;
            }
            xSemaphoreGive(mutex);
            // Wait for 5 minutes before the next request
            vTaskDelay(pdMS_TO_TICKS(CURR_CONDITIONS_REFRESH_TIME_MS)); 
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void check_error_bits(void *arg) { 
    while (true) {
        if (error_bits != prev_error_bits) {
            prev_error_bits = error_bits;
            update_seven_seg();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }   
}

void menu_btn_event(void *arg, void *usr_data) {
    ESP_LOGI(TAG_INFO, "Menu button pressed.");
    if (curr_state == STATE_MENU) {
        update_menu(true, false, false);
    } else if (curr_state == STATE_CC) {
        curr_state = STATE_MENU;
        refresh_display = true;
    }
}

void menu_up_btn_event(void *arg, void *usr_data) {
    ESP_LOGI(TAG_INFO, "Menu up button pressed.");
    if (curr_state == STATE_MENU) {
        update_menu(false, true, false);
    }
}

void menu_down_btn_event(void *arg, void *usr_data) {
    ESP_LOGI(TAG_INFO, "Menu down button pressed.");
    if (curr_state == STATE_MENU) {
        update_menu(false, false, true);
    }
}

void check_and_update_display(void *arg) {
    while (true) {
        if (refresh_display) {
            if (curr_state == STATE_CC) {
                draw_cc_view();
            } else if (curr_state == STATE_MENU) {
                draw_menu_view();
            }
            refresh_display = false;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void play_eas_tone_buzzer_1(void *arg) {
    while (true) {
        if (play_eas_tone) {
            ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, BUZZER_DUTY);
            ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            play_eas_tone = false;
        }
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void play_eas_tone_buzzer_2(void *arg) {
    while (true) {
        if (play_eas_tone) {
            ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, BUZZER_DUTY);
            ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2);
            vTaskDelay(pdMS_TO_TICKS(1000));
            play_eas_tone = false;
        }
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_2);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

//*********************************************************************************************************************

void init_pins() {
    // Seven segment register
    gpio_config_t reg_oe_config = {
        .pin_bit_mask = (1ULL << PIN_REG_OE),
        .mode = GPIO_MODE_OUTPUT,            // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    gpio_config(&reg_oe_config);

    gpio_config_t reg_rclk_config = {
        .pin_bit_mask = (1ULL << PIN_REG_RCLK),
        .mode = GPIO_MODE_OUTPUT,            // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    gpio_config(&reg_rclk_config);

    gpio_config_t reg_ser_config = {
        .pin_bit_mask = (1ULL << PIN_REG_SER),
        .mode = GPIO_MODE_OUTPUT,            // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    gpio_config(&reg_ser_config);

    ESP_ERROR_CHECK(shiftregister_gpio_init(&reg_config));

    // Incoming data LED
    gpio_config_t inc_led_config = {
        .pin_bit_mask = (1ULL << PIN_INC_DATA_LED),
        .mode = GPIO_MODE_OUTPUT,            // Set as input
        .pull_up_en = GPIO_PULLUP_DISABLE,  // Disable pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // Disable pull-down
        .intr_type = GPIO_INTR_DISABLE             // Disable interrupts
    };
    gpio_config(&inc_led_config);

    // Buttons are configured differently than normal GPIO pins.
    menu_btn_config.long_press_time = BTN_LONG_PRESS_TIME_MS;
    menu_btn_config.short_press_time = BTN_SHORT_PRESS_TIME_MS;
    menu_btn_gpio_config.gpio_num = PIN_MENU;
    menu_btn_gpio_config.active_level = BUTTON_INACTIVE;
    menu_btn_gpio_config.disable_pull = true;
    button_handle_t menu_btn;
    esp_err_t ret_menu = iot_button_new_gpio_device(&menu_btn_config, &menu_btn_gpio_config, &menu_btn);

    menu_up_btn_config.long_press_time = BTN_LONG_PRESS_TIME_MS;
    menu_up_btn_config.short_press_time = BTN_SHORT_PRESS_TIME_MS;
    menu_up_btn_gpio_config.gpio_num = PIN_MENU_UP;
    menu_up_btn_gpio_config.active_level = BUTTON_INACTIVE;
    menu_up_btn_gpio_config.disable_pull = true;

    button_handle_t menu_up_btn;
    esp_err_t ret_menu_up = iot_button_new_gpio_device(&menu_up_btn_config, &menu_up_btn_gpio_config, &menu_up_btn);

    menu_down_btn_config.long_press_time = BTN_LONG_PRESS_TIME_MS;
    menu_down_btn_config.short_press_time = BTN_SHORT_PRESS_TIME_MS;
    menu_down_btn_gpio_config.gpio_num = PIN_MENU_DOWN;
    menu_down_btn_gpio_config.active_level = BUTTON_INACTIVE;
    menu_down_btn_gpio_config.disable_pull = true;
    button_handle_t menu_down_btn;
    esp_err_t ret_menu_down = iot_button_new_gpio_device(&menu_down_btn_config, &menu_down_btn_gpio_config, 
        &menu_down_btn);

    ret_menu = iot_button_register_cb(menu_btn, BUTTON_SINGLE_CLICK, NULL, menu_btn_event, NULL);
    ESP_ERROR_CHECK(ret_menu);
    ret_menu_up = iot_button_register_cb(menu_up_btn, BUTTON_SINGLE_CLICK, NULL, menu_up_btn_event, NULL);
    ESP_ERROR_CHECK(ret_menu_up);
    ret_menu_down = iot_button_register_cb(menu_down_btn, BUTTON_SINGLE_CLICK, NULL, menu_down_btn_event, NULL);
    ESP_ERROR_CHECK(ret_menu_down);
}

void init_display() {
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = (gpio_num_t) GPIO_NUM_23,  // replace with your wiring
        .sclk_io_num = (gpio_num_t) GPIO_NUM_18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
    
    bus = dgx_spi_init_on_bus(SPI3_HOST, (gpio_num_t) GPIO_NUM_5, (gpio_num_t) GPIO_NUM_2, 40000000, 0);
    if (bus == NULL) {
        ESP_LOGE(TAG_ERROR, "dgx_spi_init failed.");
        return;
    }
    tft = dgx_ili9341_init(bus, GPIO_NUM_4, 0, 16, DgxScreenRGB);
    if (tft == NULL) {
        ESP_LOGE(TAG_ERROR, "dgx_ili9341_init failed");
        bus->dispose(bus);
        return;
    }

    dgx_ili9341_orientation(tft, DgxScreenLeftRight, DgxScreenBottomTop, true);
    
    clear_screen();
}

void init_menu() {
    	// Top of menu
	root_menu_node = create_menu_node("Menu");

	struct MenuNode *back_node = create_menu_node(MENU_OPTION_BACK);
	root_menu_node->first_child = back_node;
	
	struct MenuNode *settings_node = create_menu_node("Settings");
	settings_node->prev_sibling = back_node;
	back_node->next_sibling = settings_node;
	
	struct MenuNode *tests_node = create_menu_node("Tests");
	tests_node->prev_sibling = settings_node;
	settings_node->next_sibling = tests_node;

	// Settings
	struct MenuNode *settings_back_node = create_menu_node(MENU_OPTION_BACK);
	settings_node->first_child = settings_back_node;

	// Tests
	struct MenuNode *tests_back_node = create_menu_node(MENU_OPTION_BACK);
	tests_node->first_child = tests_back_node; 
	
	struct MenuNode *tests_eas_node = create_menu_node("EAS");
	tests_eas_node->prev_sibling = tests_back_node;
	tests_back_node->next_sibling = tests_eas_node;

	struct MenuNode *tests_cc_node = create_menu_node("Current Conditions");
	tests_cc_node->prev_sibling = tests_eas_node;
	tests_eas_node->next_sibling = tests_cc_node;

    struct MenuNode *system_info_node = create_menu_node(MENU_OPTION_SYSTEM_INFO);
    system_info_node->prev_sibling = settings_node;

    struct MenuNode *clear_error_code_node = create_menu_node(MENU_OPTION_CLEAR_ERROR_CODE);
    clear_error_code_node->prev_sibling = system_info_node;
    system_info_node->next_sibling = clear_error_code_node;

    // CC testing nodes.
    struct MenuNode *tests_cc_back_node = create_menu_node(MENU_OPTION_BACK);
    tests_cc_node->first_child = tests_cc_back_node;

    struct MenuNode *cc_clear_node = create_menu_node(MENU_OPTION_CLEAR);
    tests_cc_back_node->next_sibling = cc_clear_node;
    cc_clear_node->prev_sibling = tests_cc_back_node;

    struct MenuNode *cc_cloudy_node = create_menu_node(MENU_OPTION_CLOUDY);
    cc_clear_node->next_sibling = cc_cloudy_node;
    cc_cloudy_node->prev_sibling = cc_clear_node;

    struct MenuNode *cc_partly_cloudy_node = create_menu_node(MENU_OPTION_PARTLY_CLOUDY);
    cc_cloudy_node->next_sibling = cc_partly_cloudy_node;
    cc_partly_cloudy_node->prev_sibling = cc_cloudy_node;

    struct MenuNode *cc_showers_node = create_menu_node(MENU_OPTION_SCATTERED_SHOWERS);
    cc_partly_cloudy_node->next_sibling = cc_showers_node;
    cc_showers_node->prev_sibling = cc_partly_cloudy_node;

    struct MenuNode *cc_thunderstorm_node = create_menu_node(MENU_OPTION_THUNDERSTORM);
    cc_showers_node->next_sibling = cc_thunderstorm_node;
    cc_thunderstorm_node->prev_sibling = cc_showers_node;

	curr_parent_node = root_menu_node;
	curr_section_lead_node = back_node;
	curr_menu_node = back_node;
	curr_menu_pos = 0;
}

void init_buzzers() {
    // Buzzer 1
    ledc_timer_config_t buzzer_1_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = EAS_TONE_FREQ_1,
    };
    ledc_channel_config_t buzzer_1_channel = {
        .gpio_num = PIN_BUZZER_1,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0
    };
    ledc_timer_config(&buzzer_1_config);
    ledc_channel_config(&buzzer_1_channel);

    // Buzzer 2
    ledc_timer_config_t buzzer_2_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = EAS_TONE_FREQ_2,
    };
    ledc_channel_config_t buzzer_2_channel = {
        .gpio_num = PIN_BUZZER_2,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_2,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0
    };
    ledc_timer_config(&buzzer_2_config);
    ledc_channel_config(&buzzer_2_channel);
}

//*********************************************************************************************************************

void app_main(void) {
    // Initialize GPIO pins
    init_pins();

    // Create and initialize the display bus
    init_display();

    // Initialize the menu nodes
    init_menu();

    // Initialize buzzers
    init_buzzers();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    draw_string("Establishing network connection ...", 0, 20, DGX_WHITE(dgx_rgb_to_16), 2);
    ESP_ERROR_CHECK(example_connect());

    // Set up our tasks for our HTTP get requests.
    draw_string("Starting tasks ...", 0, 50, DGX_WHITE(dgx_rgb_to_16), 2);

    // Create mutex for HTTP response buffer.
    mutex = xSemaphoreCreateMutex();

    xTaskCreate(check_and_update_display, "check_and_update_display", 4028, NULL, 3, NULL);
    xTaskCreate(check_error_bits, "check_error_bits", 2048, NULL, 4, NULL);
    xTaskCreate(http_get_time, "http_get_time", 4028, NULL, 5, NULL);
    xTaskCreate(http_get_curr_conditions, "http_get_curr_conditions", 8192, NULL, 6, NULL);
    xTaskCreate(http_get_forecast, "http_get_forecast", 8192, NULL, 7, NULL);
    xTaskCreate(play_eas_tone_buzzer_1, "play_eas_tone_buzzer_1", 1024, NULL, 8, NULL);
    xTaskCreate(play_eas_tone_buzzer_2, "play_eas_tone_buzzer_2", 1024, NULL, 9, NULL);
}