#include <Wire.h>
#include <SPI.h>
#include <math.h>
#include <TFT_eSPI.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

MPU6050 mpu;
TFT_eSPI tft = TFT_eSPI();

struct Orientation {
  float roll;
  float pitch;
  float yaw;
};

Orientation dataOrientation;
SemaphoreHandle_t xOrientationMutex;
SemaphoreHandle_t xTFTMutex;

bool dmpReady = false;
uint8_t devStatus;
uint16_t packetSize;
uint16_t fifoCount;
uint8_t fifoBuffer[64];

Quaternion q;
VectorFloat gravity;
float ypr[3];
float prevRoll = 999.0, prevPitch = 999.0, prevYaw = 999.0;
float prevPitchStrips = 9999;
float smoothRoll = 0;

// Variable simpan koordinat untuk hapus garis Roll
int prev_horizon_x1 = -1, prev_horizon_y1 = -1, prev_horizon_x2 = -1, prev_horizon_y2 = -1;
int prev_horizon_mx1_1 = -1, prev_horizon_my1_1 = -1, prev_horizon_mx2_1 = -1, prev_horizon_my2_1 = -1;
int prev_horizon_mx1_2 = -1, prev_horizon_my1_2 = -1, prev_horizon_mx2_2 = -1, prev_horizon_my2_2 = -1;

// Variable simpan koordinat untuk hapus indikator yaw
int prev_compass_cx = -1, prev_compass_cy = -1, prev_compass_radius = 30;
int prev_compass_line_x1 = -1, prev_compass_line_y1 = -1, prev_compass_line_x2 = -1, prev_compass_line_y2 = -1;
int prev_compass_tri[6]; // 3 points (x,y) untuk segitiga arrow

void setupMPU() {
  Serial.begin(115200);
  Wire.begin();
  delay(1000);
  mpu.initialize(); 

  Serial.println("Kalibrasi aksel, giros");
  mpu.CalibrateAccel(6);
  mpu.CalibrateGyro(6);
  Serial.println("done kalibrasi!");

  devStatus = mpu.dmpInitialize();
  if (devStatus == 0) {
    mpu.setDMPEnabled(true);
    packetSize = mpu.dmpGetFIFOPacketSize();
    dmpReady = true;
    Serial.println("DMP aktif!");
  } else {
    Serial.print("Gagal inisialisasi DMP (kode error ");
    Serial.print(devStatus);
    Serial.println(")");
    while (1);
  }
}

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  xOrientationMutex = xSemaphoreCreateMutex();
  xTFTMutex = xSemaphoreCreateMutex();

  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(60, 5);
  tft.print("skala pitch");
  xTaskCreatePinnedToCore(Task_ReadMPU, "ReadMPU", 4096, NULL, 1, NULL, 0);  //MPU core0
  xTaskCreatePinnedToCore(Task_DrawLCD, "DrawLCD", 8192, NULL, 2, NULL, 1);  // drawLCD core1
}

// Task gambar display
void Task_DrawLCD(void *pvParameters) {
  while (1) {
    float roll, pitch, yaw;

    if (xSemaphoreTake(xOrientationMutex, portMAX_DELAY)) {
      roll = dataOrientation.roll;
      pitch = dataOrientation.pitch;
      yaw = dataOrientation.yaw;
      xSemaphoreGive(xOrientationMutex);
    }

    if (xSemaphoreTake(xTFTMutex, portMAX_DELAY)) {
      // Roll
      if (abs(roll - prevRoll) > 0.2) {
        drawArtificialHorizon(roll);
        drawRollText(roll);
        prevRoll = roll;
      }

      // Pitch
      if (abs(pitch - prevPitch) > 0.3) {
        drawPitchStrips(pitch);
        prevPitch = pitch;
      }

      // Yaw
      if (abs(yaw - prevYaw) > 1.0) {
        drawCompass(yaw);
        prevYaw = yaw;
      }
      xSemaphoreGive(xTFTMutex);
      }
    if (xSemaphoreTake(xOrientationMutex, portMAX_DELAY) == pdTRUE) {
      Serial.print("Yaw: ");
      Serial.print(dataOrientation.yaw, 2);
      Serial.print(" °\tPitch: ");
      Serial.print(dataOrientation.pitch, 2);
      Serial.print(" °\tRoll: ");
      Serial.print(dataOrientation.roll, 2);
      Serial.println(" °");
      xSemaphoreGive(xOrientationMutex);
      }
    vTaskDelay(30 / portTICK_PERIOD_MS);
 } 
}

//fungsi draw LCD
void drawArtificialHorizon(float rollDeg) {             //indikator Roll(garis kemiringan)
  int cx = tft.width() / 2;
  int cy = tft.height() / 2;

  float angle = radians(rollDeg);
  int len = 90;
  int x1 = cx - cos(angle) * len;
  int y1 = cy - sin(angle) * len;
  int x2 = cx + cos(angle) * len;
  int y2 = cy + sin(angle) * len;

  float perpAngle = angle + PI / 2;
  int markLen = 8;
  int mx1 = x1 - cos(perpAngle) * (markLen / 2);
  int my1 = y1 - sin(perpAngle) * (markLen / 2);
  int mx2 = x1 + cos(perpAngle) * (markLen / 2);
  int my2 = y1 + sin(perpAngle) * (markLen / 2);

  int mx3 = x2 - cos(perpAngle) * (markLen / 2);
  int my3 = y2 - sin(perpAngle) * (markLen / 2);
  int mx4 = x2 + cos(perpAngle) * (markLen / 2);
  int my4 = y2 + sin(perpAngle) * (markLen / 2);

  if (prev_horizon_x1 >= 0) {
    tft.drawLine(prev_horizon_x1, prev_horizon_y1, prev_horizon_x2, prev_horizon_y2, TFT_BLACK);
    tft.drawLine(prev_horizon_mx1_1, prev_horizon_my1_1, prev_horizon_mx2_1, prev_horizon_my2_1, TFT_BLACK);
    tft.drawLine(prev_horizon_mx1_2, prev_horizon_my1_2, prev_horizon_mx2_2, prev_horizon_my2_2, TFT_BLACK);
  }

  tft.drawLine(x1, y1, x2, y2, TFT_WHITE);
  tft.drawLine(mx1, my1, mx2, my2, TFT_WHITE);
  tft.drawLine(mx3, my3, mx4, my4, TFT_WHITE);

  prev_horizon_x1 = x1; prev_horizon_y1 = y1; prev_horizon_x2 = x2; prev_horizon_y2 = y2;
  prev_horizon_mx1_1 = mx1; prev_horizon_my1_1 = my1; prev_horizon_mx2_1 = mx2; prev_horizon_my2_1 = my2;
  prev_horizon_mx1_2 = mx3; prev_horizon_my1_2 = my3; prev_horizon_mx2_2 = mx4; prev_horizon_my2_2 = my4;
}

void drawRollText(float rollDeg) {              //teks indikator Roll
  char buf[16];
  snprintf(buf, sizeof(buf), "Roll: %.1f'", rollDeg);

  int16_t x = tft.width() - 100;
  int16_t y = 5;

  tft.fillRect(x, y, 95, 12, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.print(buf);
}

void drawPitchStrips(float pitchDeg) {          //indikator Pitch (strip dan derajat)
  int baseX = 10;
  int centerY = tft.height() / 2;

  static int lastLines[37];         
  static int lastLabelY[37];
  static bool initialized = false;
  static int lastCursorY = -1;

  int currentLines[37];
  int currentLabelY[37];

  if (!initialized) {
    for (int i = 0; i < 37; i++) {
      lastLines[i] = -1;
      lastLabelY[i] = -1;
    }
    initialized = true;
  }

  for (int i = -18; i <= 18; i++) {
    int pitchLevel = i * 10;
    int index = i + 18;

    int yOffset = centerY + (pitchLevel - pitchDeg) * 2;

  currentLines[index] = (yOffset >= 0 && yOffset <= tft.height()) ? yOffset : -1;

  if (lastLines[index] != -1) {
    int lenOld = (pitchLevel % 30 == 0) ? 15 : 10;
    tft.drawLine(baseX, lastLines[index], baseX + lenOld, lastLines[index], TFT_BLACK);
  }

  if (lastLabelY[index] != -1) {
    int labelX = baseX + ((pitchLevel % 30 == 0) ? 15 : 10) + 3;
    tft.fillRect(labelX - 1, lastLabelY[index] - 1, 24, 10, TFT_BLACK);
  }

  // gambar ulang strip
  if (yOffset >= 0 && yOffset <= tft.height()) {
    int len = (pitchLevel % 30 == 0) ? 15 : 10;
    tft.drawLine(baseX, yOffset, baseX + len, yOffset, TFT_WHITE);

    if (pitchLevel % 30 == 0) {
      int labelX = baseX + len + 3;
      int labelY = yOffset - 4;

      tft.setCursor(labelX, labelY);
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.print(abs(pitchLevel));   

      currentLabelY[index] = labelY;
    } else {
      currentLabelY[index] = -1;
    }
  } else {
    currentLabelY[index] = -1;
  }

  lastLines[index] = currentLines[index];
  lastLabelY[index] = currentLabelY[index];
}

  int cursorY = centerY;                      //indikator Pitch(panah penunjujk)
  if (lastCursorY != -1) {
    tft.fillTriangle(baseX + 33, lastCursorY, baseX + 47, lastCursorY - 5, baseX + 47, lastCursorY + 5, TFT_BLACK);
  }
  tft.fillTriangle(baseX + 33, cursorY, baseX + 47, cursorY - 5, baseX + 47, cursorY + 5, TFT_GREEN);
  lastCursorY = cursorY;
}

void drawCompass(float yawDeg) {              //indikator yaw(compass)
  static bool compassDrawn = false;
  static int cx, cy;
  const int radius = 30;

  if (!compassDrawn) {
    cx = tft.width() - radius - 15;
    cy = tft.height() - radius - 10;

    tft.drawCircle(cx, cy, radius, TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(cx - 3, cy - radius - 10); tft.print("U");
    tft.setCursor(cx + radius + 3, cy - 3);  tft.print("T");
    tft.setCursor(cx - 3, cy + radius + 3);  tft.print("S");
    tft.setCursor(cx - radius - 10, cy - 3); tft.print("B");
    compassDrawn = true;
  }

  if (prev_compass_line_x2 >= 0) {
    tft.drawLine(prev_compass_line_x1, prev_compass_line_y1, prev_compass_line_x2, prev_compass_line_y2, TFT_BLACK);
    tft.fillTriangle(prev_compass_tri[0], prev_compass_tri[1], prev_compass_tri[2], 
                     prev_compass_tri[3], prev_compass_tri[4], prev_compass_tri[5], TFT_BLACK);
  }

  float angle = radians(yawDeg - 90);     // offset -90, 0 derajat menunjuk ke atas

  int x1 = cx + cos(angle) * (radius - 5);
  int y1 = cy + sin(angle) * (radius - 5);
  tft.drawLine(cx, cy, x1, y1, TFT_RED);  // garis panah

  int tipX = x1;
  int tipY = y1;
  int side1X = cx + cos(angle + radians(10)) * (radius - 12);
  int side1Y = cy + sin(angle + radians(10)) * (radius - 12);
  int side2X = cx + cos(angle - radians(10)) * (radius - 12);
  int side2Y = cy + sin(angle - radians(10)) * (radius - 12);
  tft.fillTriangle(tipX, tipY, side1X, side1Y, side2X, side2Y, TFT_RED);

  prev_compass_line_x1 = cx;
  prev_compass_line_y1 = cy;
  prev_compass_line_x2 = x1;
  prev_compass_line_y2 = y1;
  prev_compass_tri[0] = tipX;
  prev_compass_tri[1] = tipY;
  prev_compass_tri[2] = side1X;
  prev_compass_tri[3] = side1Y;
  prev_compass_tri[4] = side2X;
  prev_compass_tri[5] = side2Y;
}

void Task_ReadMPU(void *pvParameters) {
  setupMPU();
  while (1) {
    if (!dmpReady) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    fifoCount = mpu.getFIFOCount();
    if (fifoCount < packetSize) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    if (fifoCount > 1024) {
      mpu.resetFIFO();
      Serial.println("FIFO overflow!");
      continue;
    }

    while (fifoCount >= packetSize) {
      mpu.getFIFOBytes(fifoBuffer, packetSize);
      fifoCount -= packetSize;
    }

    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

    Orientation temp;
    temp.yaw = ypr[0] * 180.0 / M_PI;
    temp.pitch = ypr[1] * 180.0 / M_PI;
    temp.roll = ypr[2] * 180.0 / M_PI;

    if (xSemaphoreTake(xOrientationMutex, portMAX_DELAY) == pdTRUE) {
      dataOrientation = temp;
      xSemaphoreGive(xOrientationMutex);
    }
    vTaskDelay(120 / portTICK_PERIOD_MS);
  }
}

void loop() {vTaskDelay(portMAX_DELAY);}