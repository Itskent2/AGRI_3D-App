#include <Arduino.h>
#include <math.h> // For sqrt
#include "LittleFS.h" // For file system

#define RX_PIN 41
#define TX_PIN 42
#define DERE_PIN 2

HardwareSerial MySerial(2);
char lastSavedFile[32] = "";

// Query: Read all 7 registers starting at 0x0000
const uint8_t QUERY_ALL[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};

void sendQuery(const uint8_t* query, size_t len) {
  digitalWrite(DERE_PIN, HIGH); // Transmit mode
  delay(10);
  MySerial.write(query, len);
  MySerial.flush();
  delay(10);
  digitalWrite(DERE_PIN, LOW); // Receive mode
}

// Refactored sensor reading: returns true if successful and fills values array
bool readSensor(int16_t* values) {
  sendQuery(QUERY_ALL, sizeof(QUERY_ALL));
  
  unsigned long start = millis();
  // Wait for first byte
  while (!MySerial.available()) {
    if (millis() - start > 2000) {
      return false; // Timeout
    }
    delay(10);
  }
  
  // Read all bytes with a timeout gap of 50ms
  uint8_t buf[128];
  int len = 0;
  unsigned long lastByteTime = millis();
  
  while (len < 128) {
    if (MySerial.available()) {
      buf[len++] = MySerial.read();
      lastByteTime = millis();
    } else {
      if (millis() - lastByteTime > 50) {
        break; // End of packet
      }
      delay(2);
    }
  }
  
  // Parse: Look for Modbus function code 03 and length 0E (14)
  for (int i = 0; i < len - 16; i++) {
    if (buf[i] == 0x03 && buf[i+1] == 0x0E) {
      values[0] = (buf[i+2] << 8) | buf[i+3];  // Moisture
      values[1] = (buf[i+4] << 8) | buf[i+5];  // Humidity/Temp
      values[2] = (buf[i+6] << 8) | buf[i+7];  // EC
      values[3] = (buf[i+8] << 8) | buf[i+9];  // pH
      values[4] = (buf[i+10] << 8) | buf[i+11]; // N
      values[5] = (buf[i+12] << 8) | buf[i+13]; // P
      values[6] = (buf[i+14] << 8) | buf[i+15]; // K
      return true;
    }
  }
  return false;
}

void runConsistencyTest() {
  Serial.println("--- Starting Consistency Test ---");
  Serial.println("Enter the number of trials (e.g., 25 or 100) and press Enter:");
  
  // Wait for Serial input
  while (!Serial.available()) {
    delay(10);
  }
  
  int numTrials = Serial.parseInt();
  // Clear Serial buffer
  while (Serial.available()) {
    Serial.read();
  }
  
  if (numTrials <= 0) {
    Serial.println("Invalid number of trials. Aborting.");
    return;
  }
  
  // Generate a random-seed-like filename as requested
  randomSeed(millis());
  long seed = random(100000000, 999999999);
  snprintf(lastSavedFile, sizeof(lastSavedFile), "/const_test_%ld.csv", seed);
  
  File file = LittleFS.open(lastSavedFile, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create file on flash!");
    return;
  }
  
  // Write header to file
  file.println("Index,Moisture(%),Humidity,EC(us/cm),pH,Nitrogen,Phosphorus,Potassium");
  
  Serial.printf("Data will be saved to: %s\n", lastSavedFile);
  Serial.printf("Running test for %d trials.\n", numTrials);
  Serial.println("Clean and dry the sensor between each reading.");
  Serial.println("Index,Moisture(%),Humidity,EC(us/cm),pH,Nitrogen,Phosphorus,Potassium");
  
  double sum[7] = {0};
  double sum_sq[7] = {0};
  int validSamples = 0;
  
  for (int i = 0; i < numTrials; i++) {
    Serial.printf("\n[Sample %d/%d] Press any key + Enter to take reading...\n", i + 1, numTrials);
    
    // Wait for Serial input
    while (!Serial.available()) {
      delay(10);
    }
    // Clear Serial buffer
    while (Serial.available()) {
      Serial.read();
    }
    
    int16_t vals[7];
    bool success = false;
    
    while (!success) {
      if (readSensor(vals)) {
        success = true;
        validSamples++;
        
        // Scale values
        double scaled[7];
        scaled[0] = vals[0] / 10.0; // Moisture
        scaled[1] = vals[1] / 10.0; // Humidity/Temp
        scaled[2] = vals[2];        // EC
        scaled[3] = vals[3] / 10.0; // pH
        scaled[4] = vals[4];        // N
        scaled[5] = vals[5];        // P
        scaled[6] = vals[6];        // K
        
        // Print CSV format to Serial
        Serial.printf("%d,%.1f,%.1f,%d,%.1f,%d,%d,%d\n", 
                      validSamples, scaled[0], scaled[1], (int)scaled[2], scaled[3], (int)scaled[4], (int)scaled[5], (int)scaled[6]);
        
        // Write CSV format to File
        file.printf("%d,%.1f,%.1f,%d,%.1f,%d,%d,%d\n", 
                    validSamples, scaled[0], scaled[1], (int)scaled[2], scaled[3], (int)scaled[4], (int)scaled[5], (int)scaled[6]);
        
        // Accumulate for Mean and SD
        for (int j = 0; j < 7; j++) {
          sum[j] += scaled[j];
          sum_sq[j] += scaled[j] * scaled[j];
        }
      } else {
        Serial.println("Read failed (Timeout)! Retrying in 1 second...");
        delay(1000);
      }
    }
  }
  
  file.close();
  Serial.printf("\nTest completed. File saved successfully as: %s\n", lastSavedFile);
  
  if (validSamples > 0) {
    Serial.println("\n--- Summary (Mean and Standard Deviation) ---");
    Serial.println("| Parameter | Mean | Std Dev (σ) |");
    Serial.println("|-----------|------|-------------|");
    
    const char* names[] = {"Moisture", "Humidity", "EC", "pH", "Nitrogen", "Phosphorus", "Potassium"};
    
    for (int j = 0; j < 7; j++) {
      double mean = sum[j] / validSamples;
      double variance = (sum_sq[j] / validSamples) - (mean * mean);
      double sd = sqrt(variance > 0 ? variance : 0);
      
      if (j == 0 || j == 1 || j == 3) { // Floats
        Serial.printf("| %-10s | %-6.2f | %-11.2f |\n", names[j], mean, sd);
      } else { // Integers
        Serial.printf("| %-10s | %-6.1f | %-11.2f |\n", names[j], mean, sd);
      }
    }
  } else {
    Serial.println("No valid samples gathered.");
  }
}

void runResponseTimeTest() {
  Serial.println("--- Starting Response Time Test (T90) ---");
  Serial.println("Enter the number of trials and press Enter:");
  
  // Wait for Serial input
  while (!Serial.available()) {
    delay(10);
  }
  
  int numTrials = Serial.parseInt();
  // Clear Serial buffer
  while (Serial.available()) {
    Serial.read();
  }
  
  if (numTrials <= 0) {
    Serial.println("Invalid number of trials. Aborting.");
    return;
  }
  
  // Generate filename for response times
  randomSeed(millis());
  long seed = random(100000000, 999999999);
  snprintf(lastSavedFile, sizeof(lastSavedFile), "/t90_test_%ld.csv", seed);
  
  File file = LittleFS.open(lastSavedFile, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create file on flash!");
    return;
  }
  
  file.println("Trial,T90_Moisture(s),T90_EC(s),StabilizationTime(s)");
  Serial.printf("Results will be saved to: %s\n", lastSavedFile);
  
  for (int trial = 0; trial < numTrials; trial++) {
    Serial.printf("\n[Trial %d/%d] Press any key + Enter when ready to start baseline reading...\n", trial + 1, numTrials);
    
    // Wait for Serial input
    while (!Serial.available()) {
      delay(10);
    }
    // Clear Serial buffer
    while (Serial.available()) {
      Serial.read();
    }
    
    Serial.println("Reading baseline for 2 seconds. Keep sensor stable.");
    
    int16_t vals[7];
    double baselineEC = 0;
    double baselineMoisture = 0;
    int count = 0;
    
    unsigned long startBaseline = millis();
    while (millis() - startBaseline < 2000) {
      if (readSensor(vals)) {
        baselineMoisture += vals[0] / 10.0;
        baselineEC += vals[2];
        count++;
      }
      delay(100);
    }
    
    if (count == 0) {
      Serial.println("Failed to read baseline. Skipping this trial.");
      continue;
    }
    
    baselineMoisture /= count;
    baselineEC /= count;
    
    Serial.printf("Baseline - Moisture: %.1f %%, EC: %.0f us/cm\n", baselineMoisture, baselineEC);
    Serial.println("Continuously reading at max speed...");
    Serial.println("Plunge the sensor into the solution now!");
    
    bool spikeDetected = false;
    unsigned long spikeTime = 0;
    
    // History buffer for T90 calculation (store more samples to look back)
    #define HISTORY_SIZE 200
    double historyMoisture[HISTORY_SIZE];
    double historyEC[HISTORY_SIZE];
    unsigned long historyTime[HISTORY_SIZE];
    int head = 0;
    int historyCount = 0;
    
    bool trialCompleted = false;
    
    while (!trialCompleted) {
      if (readSensor(vals)) {
        double currentMoisture = vals[0] / 10.0;
        double currentEC = vals[2];
        unsigned long now = millis();
        
        // Detect Spike: Look for significant increase from baseline
        if (!spikeDetected) {
          if (currentEC > baselineEC + 100 || currentMoisture > baselineMoisture + 10.0) {
            spikeDetected = true;
            spikeTime = now;
            Serial.printf("--> Spike Detected! EC: %.0f, Moisture: %.1f\n", currentEC, currentMoisture);
            Serial.println("Waiting for stabilization to find final value...");
          }
        }
        
        // Add to history
        historyMoisture[head] = currentMoisture;
        historyEC[head] = currentEC;
        historyTime[head] = now;
        head = (head + 1) % HISTORY_SIZE;
        if (historyCount < HISTORY_SIZE) historyCount++;
        
        // Check for stabilization (Less strict: 5% margin for 1 second)
        if (spikeDetected) {
          double maxM = -1, minM = 999;
          double maxE = -1, minE = 99999;
          int validHistoryCount = 0;
          
          for (int i = 0; i < historyCount; i++) {
            int idx = (head - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
            if (now - historyTime[idx] <= 1000) { // Look at last 1 second
              if (historyMoisture[idx] > maxM) maxM = historyMoisture[idx];
              if (historyMoisture[idx] < minM) minM = historyMoisture[idx];
              if (historyEC[idx] > maxE) maxE = historyEC[idx];
              if (historyEC[idx] < minE) minE = historyEC[idx];
              validHistoryCount++;
            }
          }
          
          // We need at least 1 second of data after the spike to declare stability
          if (now - spikeTime >= 1000 && validHistoryCount >= 3) {
            double avgM = (maxM + minM) / 2.0;
            double avgE = (maxE + minE) / 2.0;
            
            bool stableM = (maxM - minM) <= (avgM * 0.05); // 5% margin
            bool stableE = (maxE - minE) <= (avgE * 0.05); // 5% margin
            
            if (avgM < 5.0) stableM = (maxM - minM) <= 1.0;
            if (avgE < 50.0) stableE = (maxE - minE) <= 10.0;
            
            if (stableM && stableE) {
              Serial.println("\n--> Readings Stabilized. Calculating T90...");
              
              double finalM = currentMoisture;
              double finalE = currentEC;
              
              // Calculate 90% target
              double targetM = baselineMoisture + 0.9 * (finalM - baselineMoisture);
              double targetE = baselineEC + 0.9 * (finalE - baselineEC);
              
              unsigned long t90_M_ms = 0;
              unsigned long t90_E_ms = 0;
              
              // Search history for T90
              for (int i = 0; i < historyCount; i++) {
                int idx = (head - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;
                if (historyTime[idx] >= spikeTime) {
                  if (t90_M_ms == 0 && historyMoisture[idx] >= targetM) {
                    t90_M_ms = historyTime[idx] - spikeTime;
                  }
                  if (t90_E_ms == 0 && historyEC[idx] >= targetE) {
                    t90_E_ms = historyTime[idx] - spikeTime;
                  }
                }
              }
              
              double t90_M_sec = t90_M_ms / 1000.0;
              double t90_E_sec = t90_E_ms / 1000.0;
              
              double stab_sec = (now - spikeTime) / 1000.0;
              
              Serial.printf("Final Values - Moisture: %.1f, EC: %.0f\n", finalM, finalE);
              Serial.printf("T90 Moisture: %.2f seconds\n", t90_M_sec);
              Serial.printf("T90 EC: %.2f seconds\n", t90_E_sec);
              Serial.printf("Stabilization Time: %.2f seconds\n", stab_sec);
              
              // Save to file
              file.printf("%d,%.2f,%.2f,%.2f\n", trial + 1, t90_M_sec, t90_E_sec, stab_sec);
              file.flush();
              
              trialCompleted = true;
            }
          }
        }
      }
      delay(10); // Small yield
    }
  }
  
  file.close();
  Serial.printf("\nAll trials completed. Results saved as %s\n", lastSavedFile);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10); // Wait for USB Serial connection
  }
  
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
  }
  
  pinMode(DERE_PIN, OUTPUT);
  digitalWrite(DERE_PIN, LOW); // Start in receive mode
  
  Serial.println("\n=================================");
  Serial.println("NPK Sensor Test Suite");
  Serial.println("Commands:");
  Serial.println("  'C' - Run Consistency Test");
  Serial.println("  'R' - Run Response Time Test");
  Serial.println("  'D' - Dump Last Saved CSV File");
  Serial.println("=================================");
  
  MySerial.begin(4800, SERIAL_8N1, RX_PIN, TX_PIN);
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'C' || c == 'c') {
      runConsistencyTest();
    } else if (c == 'R' || c == 'r') {
      runResponseTimeTest();
    } else if (c == 'D' || c == 'd') {
      if (strlen(lastSavedFile) > 0) {
        Serial.printf("\n--- Dumping File: %s ---\n", lastSavedFile);
        File file = LittleFS.open(lastSavedFile, FILE_READ);
        if (file) {
          while (file.available()) {
            Serial.write(file.read());
          }
          file.close();
          Serial.println("\n--- End of File ---");
        } else {
          Serial.println("Failed to open file.");
        }
      } else {
        Serial.println("No file saved yet in this session.");
      }
    }
  }
  delay(10);
}
