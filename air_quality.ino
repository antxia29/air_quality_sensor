#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SparkFun_ENS160.h>

// --- Definiciones de Pines de la Pantalla ---
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_CS    17
#define TFT_DC    5
#define TFT_RST   16
#define TFT_BLK   4

// --- Definición del Pin del Sensor PIR ---
#define PIR_PIN   15

// --- Definiciones de Colores ---
#define ST77XX_BLACK     0x0000
#define ST77XX_WHITE     0xFFFF
#define ST77XX_RED       0xF800
#define ST77XX_GREEN     0x07E0
#define ST77XX_BLUE      0x001F
#define ST77XX_YELLOW    0xFFE0
#define ST77XX_CYAN      0x07FF
#define ST77XX_MAGENTA   0xF81F
#define ST77XX_ORANGE    0xFD20
#define ST77XX_DARKGREEN 0x03E0
#define ST77XX_NAVY      0x000F
#define ST77XX_LIGHTGREEN 0x87F0  // Verde claro
#define ST77XX_DARKRED    0x7800   // Rojo oscuro

// Crear objeto de pantalla
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Instancia del sensor ENS160
SparkFun_ENS160 ens160;

// --- Variables para el control del PIR y la pantalla ---
unsigned long lastMotionTime = 0;
const unsigned long SCREEN_OFF_DELAY = 120000;
bool screenIsOn = true;

// --- Variables para el control de la actualización ---
unsigned long lastFullScreenUpdateTime = 0;
const unsigned long FULL_SCREEN_UPDATE_INTERVAL = 120000;  // 2 minutos
unsigned long lastSensorReadTime = 0;
const unsigned long SENSOR_READ_INTERVAL = 2000;

// --- Variable para el indicador de movimiento ---
unsigned long motionIndicatorDisplayUntil = 0;
const int MOTION_INDICATOR_RADIUS = 2;
const int MOTION_INDICATOR_X = 5;
const int MOTION_INDICATOR_Y = 5;

// --- Variables globales para almacenar los valores actuales ---
float currentECO2 = 0.0;
float currentTVOC = 0.0;
uint8_t currentAQI = 0;
uint16_t currentTVOCBoxColor = ST77XX_NAVY;
String currentICAWord = "";

// --- Variables para el historial de CO2 y TVOC ---
const int HISTORY_SIZE = 150;
float co2History[HISTORY_SIZE];
float tvocHistory[HISTORY_SIZE];
int historyIndex = 0;
unsigned long lastHistorySaveTime = 0;
const unsigned long HISTORY_SAVE_INTERVAL = 30000;

// --- Variables para tendencias (media de últimos 10 mediciones) ---
const int TREND_WINDOW_SIZE = 10;
float lastECO2Trends[TREND_WINDOW_SIZE] = {0};
float lastTVOCTrends[TREND_WINDOW_SIZE] = {0};
int trendIndex = 0;
bool trendDataReady = false;
char currentECO2Trend = ' ';
char currentTVOCTrend = ' ';
char lastDisplayedECO2Trend = ' ';
char lastDisplayedTVOCTrend = ' ';

// --- Dimensiones de los cuadros de valor ---
const int16_t VALUE_BOX_HEIGHT = 70;
const int16_t VALUE_BOX_WIDTH = 140;

// --- Dimensiones y posición del gráfico ---
const int16_t GRAPH_MARGIN_X = 10;
const int16_t HEADER_TO_BOXES_MARGIN = 8;
const int16_t BOXES_TO_GRAPH_MARGIN = 5;
const int16_t GRAPH_X = GRAPH_MARGIN_X;
const int16_t GRAPH_Y = 40 + HEADER_TO_BOXES_MARGIN + VALUE_BOX_HEIGHT + BOXES_TO_GRAPH_MARGIN + 10;
const int16_t GRAPH_WIDTH = 320 - (2 * GRAPH_MARGIN_X);
const int16_t GRAPH_HEIGHT = 25;

// --- Variables para almacenar los últimos valores mostrados ---
float lastDisplayedECO2 = -1;
float lastDisplayedTVOC = -1;
uint8_t lastDisplayedAQI = 0;
String lastDisplayedICAWord = "";

// --- Estructura y función para mapear AQI a colores y texto ---
struct AQIInfo {
    uint16_t bgColor;
    uint16_t textColor;
    const char* text;
};

AQIInfo getAQIInfo(uint8_t aqi) {
    switch (aqi) {
        case 1: return {ST77XX_DARKGREEN, ST77XX_WHITE, "God"};
        case 2: return {ST77XX_LIGHTGREEN, ST77XX_BLACK, "Bien"};
        case 3: return {ST77XX_YELLOW, ST77XX_BLACK, "Regular"};
        case 4: return {ST77XX_ORANGE, ST77XX_WHITE, "Mala"};
        case 5: return {ST77XX_RED, ST77XX_WHITE, "Horrible"};
        default: return {ST77XX_BLACK, ST77XX_WHITE, "N/A"};
    }
}

// --- Función para calcular la media de un array ---
float calculateAverage(float* values, int size) {
    float sum = 0.0;
    int count = 0;
    for (int i = 0; i < size; i++) {
        if (values[i] > 0) {
            sum += values[i];
            count++;
        }
    }
    if (count == 0) return 0;
    return sum / count;
}

// --- Función para determinar la tendencia (con umbral de 50) ---
char determineTrend(float current, float average) {
    if (current > average + 50) return '↑'; // Flecha arriba
    if (current < average - 50) return '↓'; // Flecha abajo
    return '-';                             // Sin cambio
}

// --- Función para dibujar el área de la cabecera ---
void drawHeader(const char* icaWord, uint16_t bgColor, uint16_t textColor) {
  const int16_t HEADER_HEIGHT = 40;
  const int16_t NAVY_WIDTH = 320 * 0.60;
  const int16_t ICA_COLOR_WIDTH = 320 * 0.40;

  tft.fillRect(0, 0, NAVY_WIDTH, HEADER_HEIGHT, ST77XX_NAVY);
  tft.fillRect(NAVY_WIDTH, 0, ICA_COLOR_WIDTH, HEADER_HEIGHT, bgColor);

  tft.setTextWrap(false);

  // Imprimir "Calidad Aire"
  const char* titleText = "Calidad Aire";
  tft.setTextSize(2);
  int16_t titleWidth = strlen(titleText) * 12;
  int16_t titleX = (NAVY_WIDTH - titleWidth) / 2;
  if (titleX < 0) titleX = 0;
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(titleX, 10);
  tft.print(titleText);

  // Mostrar la palabra de calidad del aire
  tft.setTextSize(2);
  int16_t icaWordWidth = strlen(icaWord) * 12;
  int16_t xPosICA = NAVY_WIDTH + (ICA_COLOR_WIDTH - icaWordWidth) / 2;
  tft.setCursor(xPosICA, 10);
  tft.setTextColor(textColor);
  tft.print(icaWord);
}

// --- Función para dibujar un cuadro de valor ---
void drawValueBox(int x, int y, const char* label, float value, const char* unit, uint16_t color, char trend, uint16_t textColor) {
  tft.fillRoundRect(x, y, VALUE_BOX_WIDTH, VALUE_BOX_HEIGHT, 10, color);
  tft.drawRoundRect(x, y, VALUE_BOX_WIDTH, VALUE_BOX_HEIGHT, 10, ST77XX_WHITE);

  tft.setTextWrap(false);
  tft.setTextColor(textColor);
  
  // Etiqueta
  tft.setTextSize(2);
  tft.setCursor(x + 10, y + 5);
  tft.print(label);

  // Valor numérico
  tft.setTextSize(3);
  tft.setCursor(x + 10, y + 25);
  tft.print(value, 0);

  // Unidad
  tft.setTextSize(2);
  tft.setCursor(x + 100, y + 50);
  tft.print(unit);

  // Flecha de tendencia
  tft.setTextSize(2);
  tft.setCursor(x + VALUE_BOX_WIDTH - 25, y + 5);
  if (trend == '↑') {
    tft.print("↑");
  } else if (trend == '↓') {
    tft.print("↓");
  } else {
    tft.print("-");
  }
}

// --- Función para actualizar solo el valor y la tendencia ---
void updateValueAndTrendInBox(int x, int y, float value, const char* unit, uint16_t boxColor, char trend, uint16_t textColor) {
    // Limpiar área del valor anterior
    tft.fillRect(x + 5, y + 20, VALUE_BOX_WIDTH - 30, 30, boxColor);

    // Dibujar nuevo valor
    tft.setTextColor(textColor);
    tft.setTextSize(3);
    tft.setCursor(x + 10, y + 25);
    tft.print(value, 0);

    // Actualizar flecha de tendencia
    tft.fillRect(x + VALUE_BOX_WIDTH - 25, y + 5, 20, 20, boxColor);
    tft.setTextSize(2);
    tft.setTextColor(textColor);
    tft.setCursor(x + VALUE_BOX_WIDTH - 25, y + 5);
    if (trend == '↑') {
        tft.print("↑");
    } else if (trend == '↓') {
        tft.print("↓");
    } else {
        tft.print("-");
    }
}

// --- Función para obtener el color RGB565 basado en el valor de CO2 ---
uint16_t getCO2Color(float co2Value) {
    // Definir puntos clave con transiciones suaves cada 50ppm
    const struct {
        float co2;
        uint16_t color;
    } colorStops[] = {
        {400,  tft.color565(0, 100, 0)},     // Verde oscuro
        {450,  tft.color565(0, 120, 0)},
        {500,  tft.color565(0, 150, 0)},
        {550,  tft.color565(0, 180, 0)},
        {600,  tft.color565(50, 210, 0)},
        {650,  tft.color565(100, 230, 0)},
        {700,  ST77XX_YELLOW},         // Amarillo
        {750,  tft.color565(255, 200, 0)},  // Amarillo-naranja
        {800,  tft.color565(255, 160, 0)},  // Naranja claro
        {850,  tft.color565(255, 120, 0)},  // Naranja medio
        {900,  tft.color565(255, 80, 0)},   // Naranja oscuro
        {950,  tft.color565(255, 40, 0)},   // Rojo-naranja
        {1000, ST77XX_RED},       // Rojo
    };
    const int numStops = sizeof(colorStops) / sizeof(colorStops[0]);

    // Para valores menores o iguales a 400 ppm
    if (co2Value <= colorStops[0].co2) {
        return colorStops[0].color;
    }
    
    // Para valores mayores a 1000 ppm
    if (co2Value >= colorStops[numStops-1].co2) {
        return colorStops[numStops-1].color;
    }

    // Encontrar el rango adecuado para la interpolación
    for (int i = 0; i < numStops - 1; i++) {
        if (co2Value >= colorStops[i].co2 && co2Value <= colorStops[i+1].co2) {
            float ratio = (co2Value - colorStops[i].co2) / (colorStops[i+1].co2 - colorStops[i].co2);
            uint16_t color1 = colorStops[i].color;
            uint16_t color2 = colorStops[i+1].color;
            
            // Descomponer colores
            uint8_t r1 = (color1 >> 11) & 0x1F;
            uint8_t g1 = (color1 >> 5) & 0x3F;
            uint8_t b1 = color1 & 0x1F;
            
            uint8_t r2 = (color2 >> 11) & 0x1F;
            uint8_t g2 = (color2 >> 5) & 0x3F;
            uint8_t b2 = color2 & 0x1F;
            
            // Interpolar componentes
            uint8_t r = r1 + (r2 - r1) * ratio;
            uint8_t g = g1 + (g2 - g1) * ratio;
            uint8_t b = b1 + (b2 - b1) * ratio;
            
            return (r << 11) | (g << 5) | b;
        }
    }

    return ST77XX_WHITE;
}

// --- Función para dibujar el gráfico de CO2 ---
void drawCO2Graph(float* history, int size, int currentIndex) {
    tft.fillRect(GRAPH_X, GRAPH_Y, GRAPH_WIDTH, GRAPH_HEIGHT, ST77XX_BLACK);
    tft.drawRect(GRAPH_X, GRAPH_Y, GRAPH_WIDTH, GRAPH_HEIGHT, ST77XX_WHITE);

    bool dataFound = false;
    for (int i = 0; i < size; i++) {
        if (history[i] > 0) {
            dataFound = true;
            break;
        }
    }

    if (!dataFound) {
        tft.setTextColor(ST77XX_WHITE);
        tft.setTextSize(1);
        tft.setCursor(GRAPH_X + 10, GRAPH_Y + GRAPH_HEIGHT / 2 - 4);
        tft.print("Esperando datos CO2...");
        return;
    }

    const int STRIP_WIDTH = 2;
    int xPosition = GRAPH_X + GRAPH_WIDTH - STRIP_WIDTH; // Empezar desde el extremo derecho

    for (int i = 0; i < size; i++) {
        // Calcular el índice en orden inverso (del más reciente al más antiguo)
        int historyIdx = (currentIndex - i + size) % size;

        if (history[historyIdx] > 0) {
            float co2Value = history[historyIdx];
            uint16_t stripColor = getCO2Color(co2Value);

            // Dibujar en la posición actual
            tft.fillRect(xPosition, GRAPH_Y, STRIP_WIDTH, GRAPH_HEIGHT, stripColor);
            
            // Mover a la izquierda para el siguiente dato
            xPosition -= STRIP_WIDTH;
            
            // Si llegamos al borde izquierdo, salir
            if (xPosition < GRAPH_X) {
                break;
            }
        }
    }
}

// --- Función de Configuración ---
void setup() {
  Serial.begin(115200);
  Serial.println(F("Iniciando configuración..."));

  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);
  lastMotionTime = millis();
  screenIsOn = true;

  tft.init(170, 320);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  Serial.println(F("Pantalla TFT inicializada y borrada."));

  pinMode(PIR_PIN, INPUT);
  Serial.println(F("Sensor PIR configurado."));

  Wire.begin(21, 22);

  if (ens160.begin(0x53) == false) {
    if (ens160.begin(0x52) == false) {
      Serial.println("¡Error: ENS160 no encontrado!");
      tft.setTextColor(ST77XX_RED);
      tft.setTextSize(2);
      tft.setCursor(50, 80);
      tft.println("Error ENS160");
      while (1) delay(100);
    }
  }

  ens160.setOperatingMode(SFE_ENS160_STANDARD);
  Serial.println("ENS160 iniciado correctamente.");
  delay(500);

  for (int i = 0; i < HISTORY_SIZE; i++) {
      co2History[i] = 0.0;
      tvocHistory[i] = 0.0;
  }

  for (int i = 0; i < TREND_WINDOW_SIZE; i++) {
      lastECO2Trends[i] = 0.0;
      lastTVOCTrends[i] = 0.0;
  }

  lastFullScreenUpdateTime = millis();
  lastSensorReadTime = millis();
  lastHistorySaveTime = millis();
}

// --- Función de Bucle Principal ---
void loop() {
  unsigned long currentMillis = millis();

  // --- Control de pantalla con sensor PIR ---
  int pirState = digitalRead(PIR_PIN);

  if (pirState == HIGH) {
    lastMotionTime = currentMillis;
    motionIndicatorDisplayUntil = currentMillis + 10000;
    if (!screenIsOn) {
      digitalWrite(TFT_BLK, HIGH);
      screenIsOn = true;
      tft.fillScreen(ST77XX_BLACK);
      Serial.println("Movimiento detectado, pantalla encendida.");
      lastFullScreenUpdateTime = currentMillis - FULL_SCREEN_UPDATE_INTERVAL;
    }
  }

  if (screenIsOn && (currentMillis - lastMotionTime > SCREEN_OFF_DELAY)) {
    digitalWrite(TFT_BLK, LOW);
    screenIsOn = false;
    Serial.println("No se detectó movimiento, pantalla apagada.");
    tft.fillScreen(ST77XX_BLACK);
  }

  // --- Lectura del sensor cada 2 segundos ---
  if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = currentMillis;
    
    if (ens160.checkDataStatus()) {
      currentECO2 = ens160.getECO2();
      currentTVOC = ens160.getTVOC();
      currentAQI = ens160.getAQI();

      Serial.print("eCO2: "); Serial.print(currentECO2);
      Serial.print(" ppm, TVOC: "); Serial.print(currentTVOC);
      Serial.print(" ppb, AQI: "); Serial.println(currentAQI);
    }
  }

  // --- Guardar en historial cada 30 segundos y calcular tendencias ---
  if (currentMillis - lastHistorySaveTime >= HISTORY_SAVE_INTERVAL) {
    lastHistorySaveTime = currentMillis;
    
    // Guardar valores actuales en historial
    co2History[historyIndex] = currentECO2;
    tvocHistory[historyIndex] = currentTVOC;
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;

    // Actualizar buffers de tendencia
    lastECO2Trends[trendIndex] = currentECO2;
    lastTVOCTrends[trendIndex] = currentTVOC;
    trendIndex = (trendIndex + 1) % TREND_WINDOW_SIZE;
    
    // Marcar datos de tendencia como listos después de llenar el buffer
    if (trendIndex == 0) trendDataReady = true;

    // Calcular tendencias si hay datos suficientes
    if (trendDataReady) {
        float avgECO2 = calculateAverage(lastECO2Trends, TREND_WINDOW_SIZE);
        float avgTVOC = calculateAverage(lastTVOCTrends, TREND_WINDOW_SIZE);
        
        currentECO2Trend = determineTrend(currentECO2, avgECO2);
        currentTVOCTrend = determineTrend(currentTVOC, avgTVOC);
        
        Serial.print("Tendencia eCO2: "); Serial.print(currentECO2Trend);
        Serial.print(" (Actual: "); Serial.print(currentECO2);
        Serial.print(" - Media: "); Serial.print(avgECO2); Serial.println(")");
        
        Serial.print("Tendencia TVOC: "); Serial.print(currentTVOCTrend);
        Serial.print(" (Actual: "); Serial.print(currentTVOC);
        Serial.print(" - Media: "); Serial.print(avgTVOC); Serial.println(")");
    }
  }

  // --- Actualización de pantalla ---
  if (screenIsOn) {
    // Obtener información actual de AQI
    AQIInfo aqiInfo = getAQIInfo(currentAQI);
    currentICAWord = aqiInfo.text;

    // Actualización completa cada 2 minutos
    if (currentMillis - lastFullScreenUpdateTime >= FULL_SCREEN_UPDATE_INTERVAL) {
      tft.fillScreen(ST77XX_BLACK);
      lastFullScreenUpdateTime = currentMillis;

      // Dibujar elementos
      drawHeader(currentICAWord.c_str(), aqiInfo.bgColor, aqiInfo.textColor);
      drawValueBox(10, 48, "eCO2", currentECO2, "ppm", aqiInfo.bgColor, currentECO2Trend, aqiInfo.textColor);
      drawValueBox(170, 48, "TVOC", currentTVOC, "ppb", ST77XX_NAVY, currentTVOCTrend, ST77XX_WHITE);
      drawCO2Graph(co2History, HISTORY_SIZE, historyIndex);

      // Actualizar últimos valores mostrados
      lastDisplayedECO2 = currentECO2;
      lastDisplayedTVOC = currentTVOC;
      lastDisplayedAQI = currentAQI;
      lastDisplayedICAWord = currentICAWord;
      lastDisplayedECO2Trend = currentECO2Trend;
      lastDisplayedTVOCTrend = currentTVOCTrend;
    }

    // --- Actualización rápida de valores cada 2 segundos ---
    else {
      // Actualizar eCO2 si cambió el valor, AQI o tendencia
      if (currentAQI != lastDisplayedAQI) {
        // Cambió la calidad del aire - redibujar cuadro completo
        drawValueBox(10, 48, "eCO2", currentECO2, "ppm", aqiInfo.bgColor, currentECO2Trend, aqiInfo.textColor);
        lastDisplayedECO2 = currentECO2;
        lastDisplayedECO2Trend = currentECO2Trend;
        lastDisplayedAQI = currentAQI;
      } 
      else if (currentECO2 != lastDisplayedECO2 || currentECO2Trend != lastDisplayedECO2Trend) {
        // Solo cambió el valor o la tendencia
        updateValueAndTrendInBox(10, 48, currentECO2, "ppm", aqiInfo.bgColor, currentECO2Trend, aqiInfo.textColor);
        lastDisplayedECO2 = currentECO2;
        lastDisplayedECO2Trend = currentECO2Trend;
      }
      
      // Actualizar TVOC si cambió el valor o tendencia
      if (currentTVOC != lastDisplayedTVOC || currentTVOCTrend != lastDisplayedTVOCTrend) {
        updateValueAndTrendInBox(170, 48, currentTVOC, "ppb", ST77XX_NAVY, currentTVOCTrend, ST77XX_WHITE);
        lastDisplayedTVOC = currentTVOC;
        lastDisplayedTVOCTrend = currentTVOCTrend;
      }
      
      // Actualizar cabecera si cambió el AQI
      if (currentAQI != lastDisplayedAQI || currentICAWord != lastDisplayedICAWord) {
        drawHeader(currentICAWord.c_str(), aqiInfo.bgColor, aqiInfo.textColor);
        lastDisplayedAQI = currentAQI;
        lastDisplayedICAWord = currentICAWord;
      }
    }

    // Indicador de movimiento
    if (currentMillis < motionIndicatorDisplayUntil) {
        tft.fillCircle(MOTION_INDICATOR_X, MOTION_INDICATOR_Y, MOTION_INDICATOR_RADIUS, ST77XX_RED);
    } else {
        tft.fillCircle(MOTION_INDICATOR_X, MOTION_INDICATOR_Y, MOTION_INDICATOR_RADIUS, ST77XX_NAVY);
        motionIndicatorDisplayUntil = 0;
    }
  }

  delay(10);
}
