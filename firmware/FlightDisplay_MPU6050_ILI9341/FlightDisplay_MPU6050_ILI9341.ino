#include <Wire.h>
#include <TFT_eSPI.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"

TFT_eSPI tft = TFT_eSPI();       //TFT Display setup
MPU6050 mpu;                     //MPU6050 setup
bool dmpReady = false;
uint8_t devStatus;
uint16_t packetSize;
uint16_t fifoCount;
uint8_t fifoBuffer[64];

Quaternion q;
VectorFloat gravity;
float ypr[3]; // yaw, pitch, roll

#define FILTER_SIZE 10
float yawBuffer[FILTER_SIZE] = {0};
float pitchBuffer[FILTER_SIZE] = {0};
float rollBuffer[FILTER_SIZE] = {0};
int filterIndex = 0;

float filteredYaw = 0, filteredPitch = 0, filteredRoll = 0;

float angleDiff(float a, float b) {
  float diff = a - b;
  while (diff < -180) diff += 360;
  while (diff > 180) diff -= 360;
  return diff;
}

float average(const float* buffer) {
  float sum = 0;
  for (int i = 0; i < FILTER_SIZE; i++) sum += buffer[i];
  return sum / FILTER_SIZE;
}

unsigned long lastSensorUpdate = 0;                          // kontrol update waktu
unsigned long lastDisplayUpdate = 0;
float prevRoll = 999.0, prevPitch = 999.0, prevYaw = 999.0;

void setupMPU() {                                            // setup MPU6050 DMP
  Serial.println("Inisialisasi MPU6050 dengan DMP...");
  mpu.initialize();

  devStatus = mpu.dmpInitialize();

  if (devStatus == 0) {
    // kalibrasi MPU6050, tuning offset gyroz & accel
    mpu.setXGyroOffset(123);
    mpu.setYGyroOffset(76);
    mpu.setZGyroOffset(-85);
    mpu.setZAccelOffset(1588);

    mpu.setDMPEnabled(true);
    packetSize = mpu.dmpGetFIFOPacketSize();
    dmpReady = true;

    Serial.println("DMP ON!");
  } else {
    Serial.print("Gagal inisialisasi DMP (kode error ");
    Serial.print(devStatus);
    Serial.println(")");
    while (1); // stop program
  }
}

// Display Functions
void drawArtificialHorizon(float rollDeg) {
  int cx = tft.width() / 2;
  int cy = tft.height() / 2;

  float angle = radians(rollDeg);

  int len = 90;
  int x1 = cx - cos(angle) * len;
  int y1 = cy - sin(angle) * len;
  int x2 = cx + cos(angle) * len;
  int y2 = cy + sin(angle) * len;
  tft.drawLine(x1, y1, x2, y2, TFT_WHITE);

  float perpAngle = angle + PI / 2;
  int markLen = 8;

  int mx1 = x1 - cos(perpAngle) * (markLen / 2);
  int my1 = y1 - sin(perpAngle) * (markLen / 2);
  int mx2 = x1 + cos(perpAngle) * (markLen / 2);
  int my2 = y1 + sin(perpAngle) * (markLen / 2);
  tft.drawLine(mx1, my1, mx2, my2, TFT_WHITE);

  mx1 = x2 - cos(perpAngle) * (markLen / 2);
  my1 = y2 - sin(perpAngle) * (markLen / 2);
  mx2 = x2 + cos(perpAngle) * (markLen / 2);
  my2 = y2 + sin(perpAngle) * (markLen / 2);
  tft.drawLine(mx1, my1, mx2, my2, TFT_WHITE);
}

void drawPitchStrips(float pitchDeg) {
  int baseX = 5;
  int centerY = tft.height() / 2;

  for (int i = -180; i <= 180; i += 10) {
    int yOffset = centerY - (i - pitchDeg) * 2;
    if (yOffset < 0 || yOffset > tft.height()) continue;

    int len = (i % 30 == 0) ? 15 : 10;
    tft.drawLine(baseX, yOffset, baseX + len, yOffset, TFT_WHITE);

    if (i % 30 == 0) {
      tft.setCursor(baseX + len + 3, yOffset - 4);                                                
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.print(abs(i));
    }
  }
  int cursorY = centerY;
  tft.fillTriangle(baseX + 33, cursorY, baseX + 45, cursorY - 5, baseX + 45, cursorY + 5, TFT_MAGENTA);
}

void drawPitchLabel(const char* label) {
  int x = 50; 
  int y = 5;
  tft.setTextSize(1);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.setCursor(x, y);
  tft.print(label);
}

void drawRollText(float rollDeg) {
  char buf[16];
  snprintf(buf, sizeof(buf), "Roll: %.1f'", rollDeg);
  
  int16_t x = tft.width() - 80;
  int16_t y = 5;
  tft.fillRect(x, y, 100, 10, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.print(buf);
}

void drawCompass(float yawDeg) {
  int radius = 26;
  int cx = tft.width() - radius - 10;
  int cy = tft.height() - radius - 10;
  tft.drawCircle(cx, cy, radius, TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(cx - 3, cy - radius - 9);
  tft.print("U");
  tft.setCursor(cx + radius + 3, cy - 3);
  tft.print("T");
  tft.setCursor(cx - 3, cy + radius + 3);
  tft.print("S");
  tft.setCursor(cx - radius - 6.8, cy - 3);
  tft.print("B");

  float angle = radians(yawDeg - 90);
  int x1 = cx + cos(angle) * (radius - 5);
  int y1 = cy + sin(angle) * (radius - 5);
  tft.drawLine(cx, cy, x1, y1, TFT_RED);

  int tipX = x1;
  int tipY = y1;
  int side1X = cx + cos(angle + radians(10)) * (radius - 12);
  int side1Y = cy + sin(angle + radians(10)) * (radius - 12);
  int side2X = cx + cos(angle - radians(10)) * (radius - 12);
  int side2Y = cy + sin(angle - radians(10)) * (radius - 12);
  tft.fillTriangle(tipX, tipY, side1X, side1Y, side2X, side2Y, TFT_RED);
}

void clearArtificialHorizon() {
  int cx = tft.width() / 2;
  int cy = tft.height() / 2;
  tft.fillRect(cx - 107, cy - 91, 198, 198, TFT_BLACK);
}

void clearPitchStrips() {
  tft.fillRect(0, 0, 50, tft.height(), TFT_BLACK);
}

void clearCompass() {
  int radius = 26;
  int cx = tft.width() - radius - 10;
  int cy = tft.height() - radius - 10;
  tft.fillRect(cx - 30, cy - 30, 80, 100, TFT_BLACK);
}

void setup() {                                    // Setup
  Serial.begin(115200);
  Wire.begin();
  tft.init();                                                                                                                 
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  setupMPU();
  drawPitchLabel("Indikator Pitch");
}

void loop() {                                     // Loop
  unsigned long now = millis();

  // update sensor tiap 100 ms
  if (now - lastSensorUpdate >= 100 && dmpReady) {
    lastSensorUpdate = now;

    fifoCount = mpu.getFIFOCount();
    if (fifoCount >= packetSize) {
      if (fifoCount > 1024) {
        mpu.resetFIFO();
        Serial.println("FIFO overflow!");
        return;
      }

      while (fifoCount >= packetSize) {
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        fifoCount -= packetSize;
      }

      mpu.dmpGetQuaternion(&q, fifoBuffer);
      mpu.dmpGetGravity(&gravity, &q);
      mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

      float rawYaw   = ypr[0] * 180.0 / M_PI;
      float rawPitch = ypr[1] * 180.0 / M_PI;
      float rawRoll  = ypr[2] * 180.0 / M_PI;

      yawBuffer[filterIndex] = rawYaw;
      pitchBuffer[filterIndex] = rawPitch;
      rollBuffer[filterIndex] = rawRoll;

      filteredYaw = average(yawBuffer);
      filteredPitch = average(pitchBuffer);
      filteredRoll = average(rollBuffer);
      filterIndex = (filterIndex + 1) % FILTER_SIZE;

      Serial.printf("Yaw: %.2f, Pitch: %.2f, Roll: %.2f\n", filteredYaw, filteredPitch, filteredRoll);
    }
  }

  // Update display tiap 30 ms, hanya gambar ulang bagian penting
  if (now - lastDisplayUpdate >= 30) {
    lastDisplayUpdate = now;

    // Update horizon (roll) jika berubah > 0.5 derajat
    if (abs(filteredRoll - prevRoll) > 0.5) {
      clearArtificialHorizon();
      drawArtificialHorizon(filteredRoll);
      drawRollText(filteredRoll);
      prevRoll = filteredRoll;
    }

    // Update pitch strips jika berubah > 0.5 derajat
    if (abs(filteredPitch - prevPitch) > 0.5) {
      clearPitchStrips();
      drawPitchStrips(filteredPitch);
      prevPitch = filteredPitch;
    }

    // Update compass jika yaw berubah > 1 derajat (wrap-around)
    if (abs(angleDiff(filteredYaw, prevYaw)) > 1.0) {
      clearCompass();
      drawCompass(filteredYaw);
      prevYaw = filteredYaw;
    }
  }
} //sdabiru, scl ungu