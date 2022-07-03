#include <stdio.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include <string.h>
#include <esp_tls.h>
#include <stdlib.h>
#include <esp_event.h>
#include <esp_system.h>
#include <driver/dac.h>
#include <math.h>
#include <assert.h>

#define SSID "YOUR SSID"
#define PASS "YOUR PASSWORD"

#define PORT 443
#define URL "YOUR DATABASE FIREBASE URL.json"
#define USERNAME  "YOUR FIREBASE USERNAME" 
#define PASSWORD "YOUR FIREBASE PASSWORD"
#define DOSPI 2*3.14159265 // 2*PI

#define STACK_SIZE 1024*8   //tal vez no sea lo optimo pero con esta cantidad de memoria el core Lector funciona, con *2 no.


int sample_interval, emission_freq;
unsigned long long t_count;
const char *tag1 = "Core 1";
const char *tag2 = "Core 2";
char str[256];
float signal_out;

tcpip_adapter_ip_info_t ipinfo; 
esp_err_t ret;
esp_err_t create_task(void);
esp_err_t client_event_getemissionfreq_handler(esp_http_client_event_handle_t evt);
esp_err_t client_event_postactualfreq_handler(esp_http_client_event_handle_t evt);


void vTaskTransmisor(void *pvParameters);
void vTaskLector(void *pvParameters);
void wifi_connection();

static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void client_get_emissionfreq();
static void client_post_frecuencia_seteada(int f);
static void reconectar();
static void get_and_post();


void app_main(void)
{   
    nvs_flash_init();
    wifi_connection();
    create_task();
}



void wifi_connection()
{
    // 1 - Wi-Fi/LwIP Init Phase
    esp_netif_init();                    // TCP/IP initiation 					s1.1
    esp_event_loop_create_default();     // event loop 			                s1.2
    esp_netif_create_default_wifi_sta(); // WiFi station 	                    s1.3
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation); // 					                    s1.4

    // 2 - Wi-Fi Configuration Phase
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));  //configura el modo sta, si por default no esta seteado

    wifi_config_t wifi_configuration = {

        .sta = {
            .ssid = SSID,
            .password = PASS}};

    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);

    // 3 - Wi-Fi Start Phase
    esp_wifi_start();

    // 4- Wi-Fi Connect Phase
    esp_wifi_connect();

    vTaskDelay(5000 / portTICK_PERIOD_MS);
}

void vTaskTransmisor(void *pvParameters){

    ret = dac_output_enable(DAC_CHANNEL_1);
    ESP_ERROR_CHECK(ret);

    while (1)
    {
    
    ESP_LOGE(tag1, "TRANSMISOR DE SEÑAL \n");
    printf("frecuencia de emision:  %d \n", emission_freq);
    vTaskDelay(500 / portTICK_PERIOD_MS); // quitarle el delay para que emita de manera continua, esta en modo testeo
    t_count = esp_timer_get_time();
    signal_out = 127.5*sin(DOSPI*emission_freq*1000*t_count/1000000); //frecuencia en KHZ, t en segundos
    dac_output_voltage(DAC_CHANNEL_1, signal_out);
    
    }
    
}

void vTaskLector(void *pvParameters){

    while(1){

        ESP_LOGE(tag2, "ADQUISIDOR DE DATOS \n");

        if (esp_ip4_addr1_16(&ipinfo.ip) == 0){

            reconectar();

        }

        get_and_post();

        vTaskDelay(2000 / portTICK_PERIOD_MS);

    }

}



esp_err_t client_event_getemissionfreq_handler(esp_http_client_event_handle_t evt) //emission_freq
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        printf("Frecuencia de muestreo desde Firebase: %.*s\n", evt->data_len, (char *)evt->data);
        int frequencia = atoi(evt->data);
        printf("Seteando frecueancia de emision? en ESP32: %d\n", frequencia);
        emission_freq = frequencia; 
        break;

     default:
        break;
    }
    return ESP_OK;
}

esp_err_t client_event_postactualfreq_handler(esp_http_client_event_handle_t evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        printf("RESPUESTA ENVIADA: %.*s\n", evt->data_len, (char *)evt->data);
        break;

    default:
        break;
    }
    return ESP_OK;
}

esp_err_t create_task(void){

    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;

    xTaskCreatePinnedToCore(
        vTaskTransmisor,
        "vTaskTransmisor",
        STACK_SIZE,
        &ucParameterToPass,
        1,
        &xHandle,
        0);

    xTaskCreatePinnedToCore(
        vTaskLector,
        "vTaskLector",
        STACK_SIZE,
        &ucParameterToPass,
        1,
        &xHandle,
        1);

    return ESP_OK;
}



static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{   
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        printf("WiFi connecting ... \n");

        break;
    case WIFI_EVENT_STA_CONNECTED:
        printf("WiFi connected ... \n");

        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        printf("WiFi lost connection ... \n");
                
        break;
    case IP_EVENT_STA_GOT_IP:
        printf("WiFi got IP ... \n\n");
        break;
    default:
        break;
    }
}

static void client_get_emissionfreq()
{
    esp_http_client_config_t config_get = {
        .port = PORT,
        .url = "FIREBASE URL OF TARGET.json",
        .method = HTTP_METHOD_GET,
        .username = USERNAME,
        .password = PASSWORD,
        .event_handler = client_event_getemissionfreq_handler};
        
    esp_http_client_handle_t client = esp_http_client_init(&config_get);
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}

static void client_post_frecuencia_seteada(int f) {
    esp_http_client_config_t config_post = {
        .port = PORT,
        .url = "FIREBASE URL OF TARGET.json",
        .method = HTTP_METHOD_PUT,
        .username = USERNAME,
        .password = PASSWORD,
        .event_handler = client_event_postactualfreq_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&config_post);
    char  post_data[3];
    snprintf(post_data, 3, "%d", f);    //Int to String
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}

static void reconectar()
{
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipinfo);
    sprintf(str, "%x", ipinfo.ip.addr);
    printf("My IP: " IPSTR "\n", IP2STR(&ipinfo.ip));

    printf("Se ha perdido la conexion a wifi ... \n");
    vTaskDelay(1000/ portTICK_PERIOD_MS);
    printf("Reconectando ... \n");
    esp_wifi_connect();
    vTaskDelay(10000/ portTICK_PERIOD_MS);
}

static void get_and_post(){

    client_get_emissionfreq();

    client_post_frecuencia_seteada(emission_freq);

}



