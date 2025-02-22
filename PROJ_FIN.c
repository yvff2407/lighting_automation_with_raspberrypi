#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "inc/ssd1306.h"   // Biblioteca do OLED (conforme seu código)
#include "ws2818b.pio.h"   // Biblioteca gerada para controle dos NeoPixels

// ===========================
// DEFINIÇÕES DO OLED (128x64)
// ===========================
#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64
#define SSD1306_PAGES (SSD1306_HEIGHT / 8)
#define SSD1306_BUF_SIZE (SSD1306_WIDTH * SSD1306_PAGES)

// ===========================
// DEFINIÇÕES DA MATRIZ DE LEDS (5x5)
// ===========================
#define MATRIX_SIZE 5
#define TOTAL_SYMBOLS 25

// ===========================
// DEFINIÇÕES PARA OS NEOPIXELS (WS2818B)
// ===========================
#define LED_COUNT 25
#define LED_PIN 7

// ===========================
// DEFINIÇÕES PARA O OLED (I2C)
// ===========================
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15

// ===========================
// DEFINIÇÕES DO SENSOR BH1750
// ===========================
#define BH1750_ADDR 0x23      // Endereço I2C do BH1750
#define MODE_CONT_HRES 0x10   // Modo de operação contínuo de alta resolução
#define I2C_PORT_SENSOR i2c0
#define I2C_SDA_SENSOR 0
#define I2C_SCL_SENSOR 1

// ===========================
// DEFINIÇÕES DO BUZZER
// ===========================
#define BUZZER_PIN 21

// ===========================
// Pinos do Joystick e Botões
// ===========================
const int VRX = 26;    // Eixo horizontal do joystick
const int VRY = 27;    // Eixo vertical do joystick
const int BTN_A = 5;  // Botão A: para adicionar o dígito à sequência
const int BTN_B = 6;   // Botão B: para finalizar a sequência

// ===========================
// DEFINIÇÕES DO LED RGB
// ===========================
#define LED_RED_PIN 13
#define LED_BLUE_PIN 12
#define LED_GREEN_PIN 11

// ===========================
// DEFINIÇÕES PARA O SINAL DE DIN
// ===========================
#define MAX_DELAY_US 8333

// ===========================
// Mapeamento dos símbolos na matriz (linha por linha)
// ===========================
const char symbol_map[MATRIX_SIZE][MATRIX_SIZE] = {
    {'9', '8', '7', '6', '5'},
    {'0', '1', '2', '3', '4'},
    {'O', 'N', 'M', 'L', 'K'},
    {'F', 'G', 'H', 'I', 'J'},
    {'E', 'D', 'C', 'B', 'A'}
};

// ===========================
// Variáveis de controle do cursor na matriz
// ===========================
int cursor_x = 0, cursor_y = 0;

// ===========================
// Variáveis para a sequência digitada (máx. 5 dígitos)
// ===========================
char typed_sequence[6] = "";  // 5 dígitos + '\0'
int sequence_index = 0;
bool sequence_finalized = false;

// ===========================
// Variáveis para a leitura da altura (3 dígitos)
// ===========================
char sensor_height[4] = "";   // 3 dígitos + '\0'
int sensor_height_index = 0;
bool sensor_height_finalized = false;

// ===========================
// Estrutura para armazenar dados do ambiente
// ===========================
typedef struct {
    int codigo;
    char predio[20];
    char sala[50];
    int nivel_min_lux;
} Ambiente;

Ambiente ambientes[] = {
    {1, "Bancos", "atendimento ao público", 500},
    {2, "Bancos", "máquinas de contabilidade", 500},
    {3, "Bancos", "estatística e contabilidade", 500},
    {4, "Bancos", "salas de datilógrafas", 500},
    {5, "Bancos", "salas de gerentes", 500},
    {6, "Bancos", "salas de recepção", 150},
    {7, "Bancos", "salas de conferências", 200},
    {8, "Bancos", "guichês", 500},
    {9, "Bancos", "arquivos", 300},
    {10, "Bancos", "saguão", 150},
    {11, "Bancos", "cantinas", 150},
    {12, "Bibliotecas", "sala de leitura", 500},
    {13, "Bibliotecas", "recinto das estantes", 300},
    {14, "Bibliotecas", "fichário", 300},
    {15, "Escola", "salas de aula", 300},
    {16, "Escola", "quadros negros", 500},
    {17, "Escola", "salas de trabalhos manuais", 300},
    {18, "Escola", "laboratorios - geral", 200},
    {19, "Escola", "laboratorios - local", 500},
    {20, "Escola", "anfiteatros e auditórios - plateia", 200},
    {21, "Escola", "anfiteatros e auditórios - tribuna", 500},
    {22, "Escola", "sala de desenho", 500},
    {23, "Escola", "sala de reunioes", 200},
    {24, "Escola", "salas de educacao fisica", 150},
    {25, "Escola", "costuras e atividades semelhantes", 500},
    {26, "Escola", "artes culinarias", 200},
    {27, "Escritorios", "registros, cartografia, etc.", 1000},
    {28, "Escritorios", "desenho, engenharia mecanica e arquitetura", 1000},
    {29, "Escritorios", "desenho decorativo e esboço", 500}
};

#define TOTAL_AMBIENTES (sizeof(ambientes) / sizeof(ambientes[0]))

typedef struct {
    int encontrado;
    Ambiente ambiente;
} ResultadoBusca;

ResultadoBusca buscarAmbiente(int codigo) {
    for (int i = 0; i < TOTAL_AMBIENTES; i++) {
        if (ambientes[i].codigo == codigo) {
            ResultadoBusca res;
            res.encontrado = 1;
            res.ambiente = ambientes[i];
            return res;
        }
    }
    ResultadoBusca res;
    res.encontrado = 0;
    return res;
}

// ===========================
// Controle da matriz de LEDs (NeoPixel WS2818B)
// ===========================
struct pixel_t {
    uint8_t G, R, B;
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t;

npLED_t leds[LED_COUNT];
PIO np_pio;
uint sm;

void npInit(uint pin) {
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;
    sm = pio_claim_unused_sm(np_pio, false);
    if (sm < 0) {
        np_pio = pio1;
        sm = pio_claim_unused_sm(np_pio, true);
    }
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
    for (uint i = 0; i < LED_COUNT; ++i) {
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

void npClear() {
    for (uint i = 0; i < LED_COUNT; ++i) {
        npSetLED(i, 0, 0, 0);
    }
}

void npWrite() {
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100);
}


// Função para atualizar o gráfico de barras na matriz de LEDs, com base no duty cycle do GPIO20 (0 a 100)
void atualiza_grafico_matriz(uint duty_cycle) {
    // Limpa a matriz uma única vez:
    npClear();

    if (duty_cycle > 0 && duty_cycle < 20) {
        // Grupo A: 5º LED (índice 4) - verde escuro
        leds[4].R = 0;   leds[4].G = 50;  leds[4].B = 0;
    
    } else if (duty_cycle >= 20 && duty_cycle < 40) {
        // Faixa 60-80: Grupo A + Grupo B
        leds[4].R = 0;   leds[4].G = 50;  leds[4].B = 0;
        leds[3].R = 40;   leds[3].G = 200; leds[3].B = 0;
        leds[6].R = 40;   leds[6].G = 200; leds[6].B = 0;
    
    } else if (duty_cycle >= 40 && duty_cycle < 60) {
        // Faixa 40-60: Grupos A, B e C
        leds[4].R = 0;   leds[4].G = 50;  leds[4].B = 0;
        leds[3].R = 40;   leds[3].G = 200; leds[3].B = 0;
        leds[6].R = 40;   leds[6].G = 200; leds[6].B = 0;
        leds[2].R = 250; leds[2].G = 180; leds[2].B = 0;
        leds[7].R = 250; leds[7].G = 180; leds[7].B = 0;
        leds[12].R = 250; leds[12].G = 180; leds[12].B = 0;
  
    } else if (duty_cycle >= 60 && duty_cycle < 80) {
        // Faixa 20-40: Grupos A, B, C e D
        leds[4].R = 0;   leds[4].G = 50;  leds[4].B = 0;
        leds[3].R = 40;   leds[3].G = 200; leds[3].B = 0;
        leds[6].R = 40;   leds[6].G = 200; leds[6].B = 0;
        leds[2].R = 250; leds[2].G = 180; leds[2].B = 0;
        leds[7].R = 250; leds[7].G = 180; leds[7].B = 0;
        leds[12].R = 250; leds[12].G = 180; leds[12].B = 0;
        leds[1].R = 255; leds[1].G = 100; leds[1].B = 0;
        leds[8].R = 255; leds[8].G = 100; leds[8].B = 0;
        leds[11].R = 255; leds[11].G = 100; leds[11].B = 0;
        leds[18].R = 255; leds[18].G = 100; leds[18].B = 0;

    } else if (duty_cycle >= 80 && duty_cycle <= 100) { // duty_cycle < 20
        // Faixa 0-20: Grupos A, B, C, D e E
        leds[4].R = 0;   leds[4].G = 50;  leds[4].B = 0;
        leds[3].R = 40;   leds[3].G = 200; leds[3].B = 0;
        leds[6].R = 40;   leds[6].G = 200; leds[6].B = 0;
        leds[2].R = 250; leds[2].G = 180; leds[2].B = 0;
        leds[7].R = 250; leds[7].G = 180; leds[7].B = 0;
        leds[12].R = 250; leds[12].G = 180; leds[12].B = 0;
        leds[1].R = 255; leds[1].G = 100; leds[1].B = 0;
        leds[8].R = 255; leds[8].G = 100; leds[8].B = 0;
        leds[11].R = 255; leds[11].G = 100; leds[11].B = 0;
        leds[18].R = 255; leds[18].G = 100; leds[18].B = 0;
        leds[0].R = 255; leds[0].G = 0;   leds[0].B = 0;
        leds[9].R = 255; leds[9].G = 0;   leds[9].B = 0;
        leds[10].R = 255; leds[10].G = 0; leds[10].B = 0;
        leds[19].R = 255; leds[19].G = 0; leds[19].B = 0;
        leds[20].R = 255; leds[20].G = 0; leds[20].B = 0;
    }

    // Atualiza a matriz com os novos valores:
    npWrite();
}

// ===========================
// Funções auxiliares para o OLED
// ===========================

uint8_t display_buffer[SSD1306_BUF_SIZE];

void my_ssd1306_clear(uint8_t *buffer) {
    memset(buffer, 0, SSD1306_BUF_SIZE);
}

void my_ssd1306_update(uint8_t *buffer) {
    struct render_area full_area = {0, SSD1306_WIDTH - 1, 0, SSD1306_PAGES - 1, 0};
    calculate_render_area_buffer_length(&full_area);
    render_on_display(buffer, &full_area);
}

void display_string_on_oled(const char *str1, const char *str2, const char *str3) {
    my_ssd1306_clear(display_buffer);
    ssd1306_draw_string(display_buffer, 0, 0, (char *)str1);
    ssd1306_draw_string(display_buffer, 0, 10, (char *)str2);
    ssd1306_draw_string(display_buffer, 0, 20, (char *)str3);
    my_ssd1306_update(display_buffer);
}

// ===========================
// Funções para o SENSOR BH1750
// ===========================
void bh1750_init(i2c_inst_t *i2c) {
    gpio_set_function(0, GPIO_FUNC_I2C);  // SDA
    gpio_set_function(1, GPIO_FUNC_I2C);  // SCL
    gpio_pull_up(0);
    gpio_pull_up(1);
    uint8_t cmd = MODE_CONT_HRES;
    i2c_write_blocking(i2c, BH1750_ADDR, &cmd, 1, false);
}

uint16_t bh1750_read_lux(i2c_inst_t *i2c) {
    uint8_t data[2];
    i2c_read_blocking(i2c, BH1750_ADDR, data, 2, false);
    return ((data[0] << 8) | data[1]) / 1.2;  // Conversão para lux
}

// Função para calcular a iluminância esperada no teto
double calcularLuxNoTeto(double luxPlano, double alturaPlano, double alturaTeto) {
    return luxPlano * (alturaPlano / alturaTeto) * (alturaPlano / alturaTeto);
}

// Função de configuração do sensor BH1750 usando I2C_PORT_SENSOR:
void setup_sensor() {
    i2c_init(I2C_PORT_SENSOR, 100 * 1000);
    gpio_set_function(I2C_SDA_SENSOR, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_SENSOR, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_SENSOR);
    gpio_pull_up(I2C_SCL_SENSOR);
    bh1750_init(I2C_PORT_SENSOR);
    sleep_ms(200);
}

// ===========================
// Funções para o controle do PWM
// ===========================

volatile uint duty_cycle_gpio20 = 0;

// Função para configurar o PWM no GPIO 20
void setup_pwm_gpio20(uint32_t freq_hz, uint8_t duty_cycle) {
    gpio_set_function(20, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(20);
    pwm_set_clkdiv(slice_num, (float)125e6 / (freq_hz * 4096));
    pwm_set_wrap(slice_num, 4095);
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(20), (duty_cycle * 4095) / 100);
    pwm_set_enabled(slice_num, true);
}

// ===========================
// Funções para o controle do Buzzer
// ===========================

void pwm_init_buzzer(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 4.0f);
    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(pin, 0);
}

void play_tone(uint pin, uint frequency, uint duration_ms) {
    uint slice_num = pwm_gpio_to_slice_num(pin);
    uint32_t clock_freq = clock_get_hz(clk_sys);
    uint32_t top = clock_freq / frequency - 1;
    
    pwm_set_wrap(slice_num, top);
    pwm_set_gpio_level(pin, (top*8) / 10);
    
    sleep_ms(duration_ms);
    
    pwm_set_gpio_level(pin, 0);
    sleep_ms(50);
}

// ===========================
// Configuração dos inputs: joystick e botões
// ===========================
void setup_inputs() {
    adc_init();
    adc_gpio_init(VRX);
    adc_gpio_init(VRY);
    
    gpio_init(BTN_A);
    gpio_set_dir(BTN_A, GPIO_IN);
    gpio_pull_up(BTN_A);
    
    gpio_init(BTN_B);
    gpio_set_dir(BTN_B, GPIO_IN);
    gpio_pull_up(BTN_B);
}

// ===========================
// Funções para o controle do RGB
// ===========================
void setup_pwm(uint gpio, uint32_t freq_hz) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice_num, 125e6 / (freq_hz * 4096)); // calcula divisor
    pwm_set_wrap(slice_num, 4095);
    // Inicialmente, configura o LED apagado (duty cycle 100% -> nível = 0)
    pwm_set_gpio_level(gpio, 4095 - (100 * 4095) / 100);
    pwm_set_enabled(slice_num, true);
}

// Variável global para o temporizador de blinking do LED verde
struct repeating_timer green_blink_timer;
bool green_blinking_active = false;

// Callback que alterna o estado do LED verde (1Hz: 500ms on, 500ms off)
bool led_green_blink_callback(struct repeating_timer *t) {
    static bool state = false;
    state = !state;
    gpio_put(LED_GREEN_PIN, state); // Alterna o estado do LED verde
    return true; // Continua o temporizador
}

// ===========================
// Configuração do OLED
// ===========================
void setup_display() {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init();
    my_ssd1306_clear(display_buffer);
    my_ssd1306_update(display_buffer);
}

// ===========================
// Leitura dos eixos do Joystick
// ===========================
void joystick_read_axis(uint16_t *vrx_value, uint16_t *vry_value) {
    adc_select_input(0);
    sleep_us(2);
    *vrx_value = adc_read();
    
    adc_select_input(1);
    sleep_us(2);
    *vry_value = adc_read();
}

// ===========================
// Navegação na matriz de LEDs (mapeamento natural)
// ===========================
void move_cursor() {
    uint16_t raw_vrx, raw_vry;
    joystick_read_axis(&raw_vrx, &raw_vry);
    
    if (raw_vrx < 1000 && cursor_x > 0) {
        cursor_x--;
    }
    if (raw_vrx > 3000 && cursor_x < MATRIX_SIZE - 1) {
        cursor_x++;
    }
    if (raw_vry < 1000 && cursor_y > 0) {
        cursor_y--;
    }
    if (raw_vry > 3000 && cursor_y < MATRIX_SIZE - 1) {
        cursor_y++;
    }
    sleep_ms(200);
}

// ===========================
// Processa o botão A: adiciona o símbolo atual à sequência
// ===========================
void handle_button_A(char current_symbol) {
    static uint32_t last_press_time_A = 0;
    if (gpio_get(BTN_A) == 0) {
        if (time_us_32() - last_press_time_A > 300000) { // debounce de 300ms
            last_press_time_A = time_us_32();
            if (sequence_index < 5) {
                typed_sequence[sequence_index] = current_symbol;
                sequence_index++;
                typed_sequence[sequence_index] = '\0';
                play_tone(BUZZER_PIN, 1000, 200);
            }
        }
    }
}

// ===========================
// Processa o botão B: finaliza a sequência digitada
// ===========================
void handle_button_B() {
    static uint32_t last_press_time_B = 0;
    if (gpio_get(BTN_B) == 0) {
        if (time_us_32() - last_press_time_B > 300000) {
            last_press_time_B = time_us_32();
            sequence_finalized = true;
            play_tone(BUZZER_PIN, 1500, 200);
        }
    }
}

// ===========================
// Funções para a leitura da altura do sensor (3 dígitos)
// ===========================
void handle_button_A_height(char current_symbol) {
    static uint32_t last_press_time_A_height = 0;
    if (gpio_get(BTN_A) == 0) {
        if (time_us_32() - last_press_time_A_height > 300000) {
            last_press_time_A_height = time_us_32();
            if (sensor_height_index < 3) {
                sensor_height[sensor_height_index] = current_symbol;
                sensor_height_index++;
                sensor_height[sensor_height_index] = '\0';
                play_tone(BUZZER_PIN, 1000, 200);
            }
        }
    }
}

void handle_button_B_height() {
    static uint32_t last_press_time_B_height = 0;
    if (gpio_get(BTN_B) == 0) {
        if (time_us_32() - last_press_time_B_height > 300000) {
            last_press_time_B_height = time_us_32();
            sensor_height_finalized = true;
            play_tone(BUZZER_PIN, 1500, 200);
        }
    }
}
// ===========================
// FUNÇÕES PARA O SINAL DE ZERO-CROSS
// ===========================
/* ---------------------------
// Declarações globais
volatile absolute_time_t last_zero_cross = {0};
volatile bool zero_cross_detected = false;
volatile uint32_t atraso_pulse_us = 1000; // Atraso de 1 ms, ajuste conforme necessário

// Callback para zero-cross
void zero_cross_callback(uint gpio, uint32_t events) {
    last_zero_cross = get_absolute_time();  // Armazena o tempo do zero-cross
    zero_cross_detected = true;              // Marca que o zero-cross foi detectado
}

// Função para setup do zero-cross
void setup_zero_cross() {
    gpio_init(18);  // Inicializa o pino para detectar o zero-cross
    gpio_set_dir(18, GPIO_IN);
    gpio_pull_up(18);  // Usando pull-up para capturar a transição de queda
    gpio_set_irq_enabled_with_callback(18, GPIO_IRQ_EDGE_RISE, true, zero_cross_callback); // Detecção na borda de subida
}

void setup_din_output() {
    gpio_init(20);
    gpio_set_dir(20, GPIO_OUT);
    gpio_put(20, false);
}
-----------------------------*/
// ===========================
// Função principal
// ===========================
int main() {
    stdio_init_all();
    // Exibe a mensagem inicial "Escolha", "o", "ambiente" (uma linha para cada)
    setup_display();
    display_string_on_oled("Digite o", "codigo do", "ambiente");
    sleep_ms(2000);  // Exibe a mensagem por 2 segundos

    // Após a mensagem inicial, configura os inputs, o buzzer e os LEDs
    setup_inputs();
    npInit(LED_PIN);
    pwm_init_buzzer(BUZZER_PIN);

    Ambiente ambiente_selecionado; //Variável para guardar o ambiente

    while(true) {
        // Zera a sequência (caso haja lixo) e reinicia variáveis de controle
        strcpy(typed_sequence, "");
        sequence_index = 0;
        sequence_finalized = false;
        // Limpa a matriz de LEDs
        npClear();
        npWrite();
        // Loop de seleção do código via LED matrix
        while (!sequence_finalized) {
            move_cursor();
            npClear();
            int led_index = cursor_y * MATRIX_SIZE + cursor_x;
            npSetLED(led_index, 150, 0, 0);
            npWrite();
        
            // Obtém o símbolo "preview" da posição atual
            char current_symbol = symbol_map[cursor_y][cursor_x];
        
            // Cria uma string que mostra a sequência já confirmada concatenada com o símbolo atual
            char display_str[10];
            strcpy(display_str, typed_sequence);
            if (sequence_index < 5) {
                int len = strlen(display_str);
                display_str[len] = current_symbol;
                display_str[len+1] = '\0';
            }
            // Exibe a string no OLED iniciando na posição (0, 0)
            display_string_on_oled(display_str, "", "");
        
            // Processa os botões para armazenar (BTN_A) ou finalizar (BTN_B) a sequência
            handle_button_A(current_symbol);
            handle_button_B();
        }
    
        // Quando a sequência estiver finalizada, converte para número e busca o ambiente correspondente
        int codigo = atoi(typed_sequence);
        ResultadoBusca res = buscarAmbiente(codigo);
    
        char result_str1[50];
        char result_str2[50];
        char result_str3[50];
        if (res.encontrado) {
            ambiente_selecionado = res.ambiente;
            // Exibe o nível mínimo de iluminação exigido
            sprintf(result_str1, "Lum min:");
            sprintf(result_str2, "%d lux", res.ambiente.nivel_min_lux);
            sprintf(result_str3, "");
            display_string_on_oled(result_str1, result_str2, result_str3);
            sleep_ms(2000);
            break; 
        } else {
            sprintf(result_str1, "Codigo nao");
            sprintf(result_str2,  "encontrado!");
            sprintf(result_str3, "Tente novamente");
            display_string_on_oled(result_str1, result_str2, result_str3);
            sleep_ms(2000);
        }
    }
    
    // Leitura da altura do sensor 
    display_string_on_oled("Digite a","altura do sensor", "");
    sleep_ms(2000);

    // Loop para garantir que a entrada da altura seja válida
    bool valid_height_input = false;
    while (!valid_height_input) {
        // Reinicia as variáveis para a leitura da altura
        strcpy(sensor_height, "");
        sensor_height_index = 0;
        sensor_height_finalized = false;
    
        // Loop de seleção do valor de altura (3 dígitos)
        while (!sensor_height_finalized) {
            move_cursor();
            npClear();
            int led_index = cursor_y * MATRIX_SIZE + cursor_x;
            npSetLED(led_index, 150, 0, 0);
            npWrite();
        
            // Obtém o símbolo "preview" da posição atual
            char current_symbol = symbol_map[cursor_y][cursor_x];
        
            // Cria uma string que mostra os dígitos já confirmados concatenados com o símbolo atual
            char height_display[10];
            strcpy(height_display, sensor_height);
            if (sensor_height_index < 3) {
                int len = strlen(height_display);
                height_display[len] = current_symbol;
                height_display[len+1] = '\0';
            }
            // Exibe a string no OLED iniciando na posição (0, 0)
            display_string_on_oled(height_display, "", "");
        
            // Processa os botões para armazenar (BTN_A) ou finalizar (BTN_B) a entrada da altura
            handle_button_A_height(current_symbol);
            handle_button_B_height();
        }
    
        // Validação: verifica se os 3 caracteres são dígitos
        bool valid = true;
        for (int i = 0; i < 3; i++) {
            if (!isdigit(sensor_height[i])) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            // Exibe mensagem de erro e reinicia a leitura da altura
            display_string_on_oled("Altura invalida!", "", "Tente novamente");
            sleep_ms(2000);
        } else {
            valid_height_input = true;
        }
    }

    // Converte a string da altura para número e separa em metros e centímetros
    int height_val = atoi(sensor_height);  // Por exemplo, "345" se torna 345
    int metros = sensor_height[0] - '0';       // Primeiro dígito = metros
    int centimetros = atoi(sensor_height + 1); // Últimos dois dígitos = centímetros
    char height_result[50];
    sprintf(height_result, "%d m %d cm", metros, centimetros);
    display_string_on_oled("Altura escrita", height_result, "");
    sleep_ms(2000);
    
    // Apaga a matriz de LEDs e interrompe a leitura do joystick para esta fase:
    npClear();
    npWrite();
    
    // --- Leitura dos dados do sensor BH1750 ---
    // A lógica: o valor de lux min (do ambiente selecionado) é usado para calcular o valor esperado para o sensor
    double luxMin = ambiente_selecionado.nivel_min_lux;   // Lux mínimo do ambiente selecionado
    double alturaPlano = 0.75;                      // Altura do plano de trabalho (em metros)
    double alturaTeto = metros + ((double)centimetros / 100.0); // Altura do sensor em metros (teto)
    double luxEsperado = calcularLuxNoTeto(luxMin, alturaPlano, alturaTeto);

    // Inicializa o sensor BH1750 
    setup_sensor();
    sleep_ms(200);
    // Configura os três LEDs indicadores:
    setup_pwm(LED_RED_PIN, 10000);    // LED vermelho: 10 kHz
    setup_pwm(LED_GREEN_PIN, 10000); // LED verde: 10 kHz
    setup_pwm(LED_BLUE_PIN, 10000);   // LED azul: 10 kHz
    

    while (1) {       
        uint16_t luxCapturado = bh1750_read_lux(i2c0);
        char lux_esperado_str[30], lux_capturado_str[30], percentual_dutycicle[30];
        sprintf(lux_esperado_str, "Lux esp: %.1f", luxEsperado);
        sprintf(lux_capturado_str, "Lux med: %u", luxCapturado);
        sprintf(percentual_dutycicle, "Duty cicle: %u", duty_cycle_gpio20);
        display_string_on_oled(lux_esperado_str, lux_capturado_str, percentual_dutycicle);
        // Verifica a condição para acender o LED correto:
        if (luxCapturado < 0.9 * luxEsperado) {
            // Condição: lux medido menor que 0,9x do esperado → LED vermelho aceso
            if (duty_cycle_gpio20 < 100) { // Garante que não passe de 5% (máximo de potência)
                duty_cycle_gpio20 += 5;
            }
            atualiza_grafico_matriz(duty_cycle_gpio20);
            // Garante que o LED verde não esteja piscando:
            if (green_blinking_active) {
                cancel_repeating_timer(&green_blink_timer);
                green_blinking_active = false;
                // Reconfigura o LED verde para PWM:
                setup_pwm(LED_GREEN_PIN, 10000);
                gpio_set_function(LED_GREEN_PIN, GPIO_FUNC_PWM);
                
            }
            // Atualiza os duty cycles para vermelho aceso, azul e verde apagados:
            pwm_set_gpio_level(LED_RED_PIN, 4095 - (25 * 4095)/100);
            pwm_set_gpio_level(LED_BLUE_PIN, 4095 - (100 * 4095)/100);
            pwm_set_gpio_level(LED_GREEN_PIN, 4095 - (100 * 4095)/100);
            // Se o LED vermelho estiver aceso, aumente a potência
              
        } else if (luxCapturado >= 0.9 * luxEsperado && luxCapturado <= 1.1 * luxEsperado) {
            if (duty_cycle_gpio20 < 100) { // Garante que não passe de 5% (máximo de potência)
                duty_cycle_gpio20 += 5;   
            }
            atualiza_grafico_matriz(duty_cycle_gpio20);
            // Condição: lux medido entre 0,9x e 1,1x → LED azul aceso
            
            if (green_blinking_active) {
                cancel_repeating_timer(&green_blink_timer);
                green_blinking_active = false;
                setup_pwm(LED_GREEN_PIN, 10000);
                gpio_set_function(LED_GREEN_PIN, GPIO_FUNC_PWM);
                pwm_set_gpio_level(LED_GREEN_PIN, 4095 - (100 * 4095)/100);
            }
            pwm_set_gpio_level(LED_RED_PIN, 4095 - (100 * 4095)/100);
            pwm_set_gpio_level(LED_BLUE_PIN, 4095 - (25 * 4095)/100);
            pwm_set_gpio_level(LED_GREEN_PIN, 4095 - (100 * 4095)/100);
            
        } else if (luxCapturado > 1.1 * luxEsperado && luxCapturado <= 1.5 * luxEsperado) {
            // Condição: lux medido entre 1,1x e 1,5x → LED verde aceso continuamente
            atualiza_grafico_matriz(duty_cycle_gpio20);
            if (green_blinking_active) {
                cancel_repeating_timer(&green_blink_timer);
                green_blinking_active = false;
                setup_pwm(LED_GREEN_PIN, 10000);
                gpio_set_function(LED_GREEN_PIN, GPIO_FUNC_PWM);
            }
            pwm_set_gpio_level(LED_RED_PIN, 4095 - (100 * 4095)/100);
            pwm_set_gpio_level(LED_BLUE_PIN, 4095 - (100 * 4095)/100);
            pwm_set_gpio_level(LED_GREEN_PIN, 4095 - (25 * 4095)/100);
            
        } else if (luxCapturado > 1.5 * luxEsperado) {
            // Condição: lux medido maior que 1,5x do esperado → LED verde deve piscar a 1Hz
            // Se o LED verde está piscando, diminui a potência
            if (duty_cycle_gpio20 > 0) { // Garante que não passe de 100% (desligado)
                duty_cycle_gpio20 -= 5;   
            }
            atualiza_grafico_matriz(duty_cycle_gpio20);
            // Se o blinking ainda não estiver ativo, reconfigure o pino e inicie o temporizador
            if (!green_blinking_active) {
                // Primeiro, desabilite o PWM para LED verde e reconfigure o pino como GPIO
                pwm_set_enabled(pwm_gpio_to_slice_num(LED_GREEN_PIN), false);
                gpio_set_function(LED_GREEN_PIN, GPIO_FUNC_SIO);
                gpio_init(LED_GREEN_PIN);
                gpio_set_dir(LED_GREEN_PIN, GPIO_OUT);
                // Inicia o temporizador para piscar o LED verde (500ms on/off → 1Hz)
                add_repeating_timer_ms(500, led_green_blink_callback, NULL, &green_blink_timer);
                green_blinking_active = true;
            // Para os demais LEDs, mantenha-os apagados:
            pwm_set_gpio_level(LED_RED_PIN, 4095 - (100 * 4095)/100);
            pwm_set_gpio_level(LED_BLUE_PIN, 4095 - (100 * 4095)/100);
            
            }
            
        }
        setup_pwm_gpio20(120, duty_cycle_gpio20); 
        sleep_ms(2000);   
    }
    
    return 0;
    
}