

#include "esp_camera.h"
#include "WiFi.h"
#include "WebServer.h"
#include "ESPmDNS.h"
#include <Firebase_ESP_Client.h>
#include <time.h>

const char* ssid = "Prateek";
const char* password = "justdoelectronics@#12345";
const char* mdnsName = "portablecamera";

#define FIREBASE_HOST "attendance-ed3eb-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "PQbHd2LvThsAr7bAi2Cj4KSbbIJlA59xPvkC6WwU"

// NTP Server configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 5.5 * 3600; // GMT+5:30 for India
const int daylightOffset_sec = 0;

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Camera pins for AI-Thinker ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WebServer server(80);

// Face recognition settings
#define ENROLL_CONFIRM_TIMES 5
#define FACE_ID_SAVE_NUMBER 7

// User database
struct User {
  int id;
  String name;
  String role;
  String department;
  bool isEnrolled;
  String lastAttendance;
};

User users[7] = {
  {0, "Unknown", "Unknown", "Unknown", false, ""},
  {1, "Prateek Singh", "Admin", "Electronics", false, ""},
  {2, "Raj Sharma", "Employee", "IT", false, ""},
  {3, "Priya Patel", "Manager", "HR", false, ""},
  {4, "Amit Kumar", "Employee", "Development", false, ""},
  {5, "Sneha Gupta", "Employee", "Design", false, ""},
  {6, "Vikram Joshi", "Employee", "Testing", false, ""}
};

// Attendance record structure
struct AttendanceRecord {
  int userId;
  String userName;
  String date;
  String time;
  String datetime;
};

// Global variables
bool faceDetectionEnabled = false;
bool faceRecognitionEnabled = false;
bool isEnrolling = false;
int currentFaceId = 0;
bool controlState = true;
bool attendanceEnabled = true;
unsigned long lastAttendanceTime = 0;
const unsigned long ATTENDANCE_COOLDOWN = 30000; // 30 seconds cooldown

// Local attendance storage
AttendanceRecord attendanceRecords[100];
int attendanceCount = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32-CAM Face Recognition Attendance System Starting...");

  // Camera configuration
  camera_config_t config_cam;
  config_cam.ledc_channel = LEDC_CHANNEL_0;
  config_cam.ledc_timer = LEDC_TIMER_0;
  config_cam.pin_d0 = Y2_GPIO_NUM;
  config_cam.pin_d1 = Y3_GPIO_NUM;
  config_cam.pin_d2 = Y4_GPIO_NUM;
  config_cam.pin_d3 = Y5_GPIO_NUM;
  config_cam.pin_d4 = Y6_GPIO_NUM;
  config_cam.pin_d5 = Y7_GPIO_NUM;
  config_cam.pin_d6 = Y8_GPIO_NUM;
  config_cam.pin_d7 = Y9_GPIO_NUM;
  config_cam.pin_xclk = XCLK_GPIO_NUM;
  config_cam.pin_pclk = PCLK_GPIO_NUM;
  config_cam.pin_vsync = VSYNC_GPIO_NUM;
  config_cam.pin_href = HREF_GPIO_NUM;
  config_cam.pin_sscb_sda = SIOD_GPIO_NUM;
  config_cam.pin_sscb_scl = SIOC_GPIO_NUM;
  config_cam.pin_pwdn = PWDN_GPIO_NUM;
  config_cam.pin_reset = RESET_GPIO_NUM;
  config_cam.xclk_freq_hz = 20000000;
  config_cam.pixel_format = PIXFORMAT_JPEG;
  config_cam.frame_size = FRAMESIZE_SVGA;
  config_cam.jpeg_quality = 12;
  config_cam.fb_count = 1;

  // Initialize camera
  esp_err_t err = esp_camera_init(&config_cam);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    delay(5000);
    ESP.restart();
    return;
  }
  Serial.println("Camera initialized successfully!");

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());

  // Initialize and get time from NTP server
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Waiting for NTP time sync...");
  
  // Wait for time to be set
  time_t now;
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time, retrying...");
    delay(2000);
    retry++;
    if (retry > 10) {
      Serial.println("NTP timeout, continuing without accurate time");
      break;
    }
  }
  
  // Print current time
  getLocalTime(&timeinfo);
  Serial.print("Current time: ");
  Serial.println(getDateTimeString());

  // Initialize Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Set database read/write settings
  Firebase.RTDB.setReadTimeout(&fbdo, 1000 * 60);
  Firebase.RTDB.setwriteSizeLimit(&fbdo, "tiny");
  
  Serial.println("Firebase initialized");

  // Test Firebase connection
  if (Firebase.RTDB.setInt(&fbdo, "test/connection", 1)) {
    Serial.println("Firebase connection successful!");
  } else {
    Serial.println("Firebase connection failed: " + fbdo.errorReason());
  }

  // Initialize mDNS
  if (!MDNS.begin(mdnsName)) {
    Serial.println("Error starting mDNS");
  } else {
    Serial.println("mDNS started: http://" + String(mdnsName) + ".local");
  }

  // Setup server routes
  server.on("/", handleRoot);
  server.on("/capture", handleCapture);
  server.on("/stream", handleStream);
  server.on("/control", handleControl);
  server.on("/status", handleStatus);
  server.on("/attendance", handleAttendance);
  server.on("/users", handleUsers);
  server.on("/export", handleExport);

  server.begin();
  Serial.println("HTTP server started");
  Serial.println("System ready! Open http://" + WiFi.localIP().toString() + " in your browser");
}

// Function to get current date and time as string
String getDateTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time not available";
  }
  
  char timeString[64];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeString);
}

// Function to get current date as string
String getDateString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Date not available";
  }
  
  char dateString[64];
  strftime(dateString, sizeof(dateString), "%Y-%m-%d", &timeinfo);
  return String(dateString);
}

// Function to get current time as string
String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time not available";
  }
  
  char timeString[64];
  strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
  return String(timeString);
}

// HTML Page Handler
void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
      <title>ESP32-CAM Face Recognition Attendance</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        body { font-family: Arial; max-width: 1200px; margin: 0 auto; padding: 20px; background: #f5f5f5; }
        .container { background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; margin-bottom: 30px; }
        .control-panel { background: #f8f9fa; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
        .button-group { margin: 15px 0; }
        button { padding: 12px 24px; margin: 5px; font-size: 16px; border: none; border-radius: 5px; cursor: pointer; }
        .btn-primary { background: #007bff; color: white; }
        .btn-success { background: #28a745; color: white; }
        .btn-warning { background: #ffc107; color: black; }
        .btn-danger { background: #dc3545; color: white; }
        .btn-info { background: #17a2b8; color: white; }
        button:hover { opacity: 0.9; }
        .status { padding: 15px; margin: 10px 0; border-radius: 5px; font-weight: bold; }
        .status-connected { background: #d4edda; color: #155724; }
        .status-disconnected { background: #f8d7da; color: #721c24; }
        .status-warning { background: #fff3cd; color: #856404; }
        .video-container { text-align: center; margin: 20px 0; }
        #stream { max-width: 100%; border: 3px solid #ddd; border-radius: 8px; }
        .switch { position: relative; display: inline-block; width: 60px; height: 34px; margin: 0 10px; }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background: #ccc; transition: .4s; border-radius: 34px; }
        .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background: white; transition: .4s; border-radius: 50%; }
        input:checked + .slider { background: #007bff; }
        input:checked + .slider:before { transform: translateX(26px); }
        .form-group { margin: 15px 0; }
        label { display: inline-block; width: 200px; font-weight: bold; }
        select { padding: 8px; border: 1px solid #ddd; border-radius: 4px; font-size: 16px; }
        .attendance-log { max-height: 300px; overflow-y: auto; border: 1px solid #ddd; padding: 10px; border-radius: 5px; background: #f8f9fa; }
        .log-entry { padding: 8px; margin: 5px 0; background: white; border-radius: 4px; border-left: 4px solid #007bff; }
        .current-time { background: #e9ecef; padding: 10px; border-radius: 5px; text-align: center; font-weight: bold; margin: 10px 0; }
      </style>
  </head>
  <body>
      <div class="container">
          <h1>ESP32-CAM Face Recognition Attendance System</h1>
          
          <div class="current-time">
             Current Time: <span id="currentTime">Loading...</span>
          </div>
          
          <div class="control-panel">
              <div class="status" id="wifiStatus">System Ready</div>
              
              <div class="button-group">
                  <button class="btn-primary" onclick="capturePhoto()">Capture Photo</button>
                  <button class="btn-success" onclick="startStream()"> Start Stream</button>
                  <button class="btn-danger" onclick="stopStream()">Stop Stream</button>
                  <button class="btn-info" onclick="viewAttendance()"> View Attendance</button>
                  <button class="btn-warning" onclick="syncTime()"> Sync Time</button>
              </div>

              <div class="form-group">
                  <label>Face Detection:</label>
                  <label class="switch">
                      <input type="checkbox" id="faceDetection" onchange="toggleFaceDetection()">
                      <span class="slider"></span>
                  </label>
              </div>

              <div class="form-group">
                  <label>Face Recognition:</label>
                  <label class="switch">
                      <input type="checkbox" id="faceRecognition" onchange="toggleFaceRecognition()">
                      <span class="slider"></span>
                  </label>
              </div>

              <div class="form-group">
                  <label>Attendance System:</label>
                  <label class="switch">
                      <input type="checkbox" id="attendanceSystem" checked onchange="toggleAttendanceSystem()">
                      <span class="slider"></span>
                  </label>
              </div>

              <div class="form-group">
                  <label>Enroll User:</label>
                  <select id="userSelect">
                    <option value="1">Prateek Singh (Admin)</option>
                    <option value="2">Raj Sharma (Employee)</option>
                    <option value="3">Priya Patel (Manager)</option>
                    <option value="4">Amit Kumar (Employee)</option>
                    <option value="5">Sneha Gupta (Employee)</option>
                    <option value="6">Vikram Joshi (Employee)</option>
                  </select>
                  <button class="btn-warning" onclick="enrollUser()">Enroll Face</button>
                  <button class="btn-danger" onclick="deleteAllFaces()">Delete All Faces</button>
              </div>

              <div class="form-group">
                  <label>Firebase Control:</label>
                  <label class="switch">
                      <input type="checkbox" id="firebaseControl" checked onchange="toggleFirebaseControl()">
                      <span class="slider"></span>
                  </label>
              </div>
          </div>

          <div class="video-container">
              <img id="stream" src="" alt="Camera Stream">
          </div>

          <div id="recognitionResult" class="status"></div>
          
          <div id="attendanceSection" style="display:none;">
              <h2>Today's Attendance - <span id="currentDate"></span></h2>
              <div id="attendanceLog" class="attendance-log">
                  Loading attendance data...
              </div>
          </div>
      </div>

      <script>
          function updateStatus(message, type) {
              const statusDiv = document.getElementById('wifiStatus');
              statusDiv.textContent = message;
              statusDiv.className = 'status ' + (type === 'success' ? 'status-connected' : type === 'warning' ? 'status-warning' : 'status-disconnected');
          }

          function updateCurrentTime() {
              const now = new Date();
              const timeString = now.toLocaleString('en-IN', { 
                  timeZone: 'Asia/Kolkata',
                  year: 'numeric',
                  month: '2-digit',
                  day: '2-digit',
                  hour: '2-digit',
                  minute: '2-digit',
                  second: '2-digit',
                  hour12: false
              });
              document.getElementById('currentTime').textContent = timeString;
              
              const dateString = now.toLocaleDateString('en-IN', {
                  timeZone: 'Asia/Kolkata',
                  year: 'numeric',
                  month: '2-digit',
                  day: '2-digit'
              });
              document.getElementById('currentDate').textContent = dateString;
          }

          function capturePhoto() {
              document.getElementById('stream').src = '/capture?' + new Date().getTime();
              updateStatus('Photo captured', 'success');
          }

          function startStream() {
              document.getElementById('stream').src = '/stream';
              updateStatus('Live stream started', 'success');
          }

          function stopStream() {
              document.getElementById('stream').src = '';
              updateStatus('Stream stopped', 'success');
          }

          function syncTime() {
              updateCurrentTime();
              updateStatus('Time synced with browser', 'success');
          }

          function viewAttendance() {
              fetch('/attendance')
                  .then(response => response.text())
                  .then(data => {
                      document.getElementById('attendanceLog').innerHTML = data;
                      document.getElementById('attendanceSection').style.display = 'block';
                  });
              updateStatus('Attendance data loaded', 'success');
          }

          function toggleFaceDetection() {
              const enabled = document.getElementById('faceDetection').checked;
              fetch('/control?face_detect=' + (enabled ? '1' : '0'));
              updateStatus('Face detection ' + (enabled ? 'enabled' : 'disabled'), 'success');
          }

          function toggleFaceRecognition() {
              const enabled = document.getElementById('faceRecognition').checked;
              fetch('/control?face_recognize=' + (enabled ? '1' : '0'));
              updateStatus('Face recognition ' + (enabled ? 'enabled' : 'disabled'), 'success');
          }

          function toggleAttendanceSystem() {
              const enabled = document.getElementById('attendanceSystem').checked;
              fetch('/control?attendance_system=' + (enabled ? '1' : '0'));
              updateStatus('Attendance system ' + (enabled ? 'enabled' : 'disabled'), 'success');
          }

          function enrollUser() {
              const userId = document.getElementById('userSelect').value;
              fetch('/control?enroll_user=' + userId);
              updateStatus('Enrolling user ID: ' + userId, 'warning');
          }

          function deleteAllFaces() {
              if(confirm('Are you sure you want to delete all enrolled faces?')) {
                  fetch('/control?delete_faces=1');
                  updateStatus('All faces deleted', 'success');
              }
          }

          function toggleFirebaseControl() {
              const enabled = document.getElementById('firebaseControl').checked;
              fetch('/control?firebase_control=' + (enabled ? '1' : '0'));
              updateStatus('Firebase control ' + (enabled ? 'enabled' : 'disabled'), 'success');
          }

          // Initialize
          updateStatus('System Ready - Connect to camera stream', 'success');
          startStream();
          updateCurrentTime();
          setInterval(updateCurrentTime, 1000);
          
          // Update recognition results periodically
          setInterval(() => {
              fetch('/status')
                  .then(response => response.json())
                  .then(data => {
                      if(data.recognized_face !== undefined && data.recognized_face !== -1) {
                          const resultDiv = document.getElementById('recognitionResult');
                          resultDiv.textContent = ' Recognized: ' + data.recognized_name + 
                                                ' | Role: ' + data.recognized_role +
                                                ' | Date: ' + data.attendance_date +
                                                ' | Time: ' + data.attendance_time;
                          resultDiv.className = 'status status-connected';
                      }
                  });
          }, 3000);
      </script>
  </body>
  </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleStream() {
  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) break;

    String header = "--frame\r\n";
    header += "Content-Type: image/jpeg\r\n";
    header += "Content-Length: " + String(fb->len) + "\r\n\r\n";
    
    server.sendContent(header);
    client.write((char*)fb->buf, fb->len);
    server.sendContent("\r\n");
    
    esp_camera_fb_return(fb);
    delay(100);
    
    if (!client.connected()) break;
  }
}

void handleControl() {
  String message = "";
  
  for (int i = 0; i < server.args(); i++) {
    String argName = server.argName(i);
    String argValue = server.arg(i);
    
    if (argName == "face_detect") {
      faceDetectionEnabled = (argValue == "1");
      message = "Face detection " + String(faceDetectionEnabled ? "enabled" : "disabled");
    }
    else if (argName == "face_recognize") {
      faceRecognitionEnabled = (argValue == "1");
      message = "Face recognition " + String(faceRecognitionEnabled ? "enabled" : "disabled");
    }
    else if (argName == "attendance_system") {
      attendanceEnabled = (argValue == "1");
      message = "Attendance system " + String(attendanceEnabled ? "enabled" : "disabled");
    }
    else if (argName == "enroll_user") {
      int userId = argValue.toInt();
      if (userId >= 1 && userId <= 6) {
        enrollUser(userId);
        message = "Enrolled user: " + users[userId].name;
      }
    }
    else if (argName == "delete_faces") {
      for(int i = 1; i <= 6; i++) {
        users[i].isEnrolled = false;
        users[i].lastAttendance = "";
      }
      message = "All faces deleted and attendance cleared";
    }
    else if (argName == "firebase_control") {
      controlState = (argValue == "1");
      message = "Firebase control " + String(controlState ? "enabled" : "disabled");
    }
  }
  
  server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"" + message + "\"}");
}

void handleStatus() {
  // Simulate face recognition results
  int recognizedFace = random(-1, 4);
  String statusMessage = "";
  
  if (recognizedFace >= 1 && recognizedFace <= 6) {
    if (attendanceEnabled && (millis() - lastAttendanceTime > ATTENDANCE_COOLDOWN)) {
      markAttendance(recognizedFace);
      lastAttendanceTime = millis();
      statusMessage = "Attendance marked for " + users[recognizedFace].name;
    }
  }

  String json = "{";
  json += "\"face_detection\":" + String(faceDetectionEnabled ? "true" : "false") + ",";
  json += "\"face_recognition\":" + String(faceRecognitionEnabled ? "true" : "false") + ",";
  json += "\"attendance_enabled\":" + String(attendanceEnabled ? "true" : "false") + ",";
  json += "\"recognized_face\":" + String(recognizedFace) + ",";
  json += "\"recognized_name\":\"" + (recognizedFace >= 1 ? users[recognizedFace].name : "Unknown") + "\",";
  json += "\"recognized_role\":\"" + (recognizedFace >= 1 ? users[recognizedFace].role : "Unknown") + "\",";
  json += "\"attendance_date\":\"" + getDateString() + "\",";
  json += "\"attendance_time\":\"" + getTimeString() + "\",";
  json += "\"status_message\":\"" + statusMessage + "\",";
  json += "\"firebase_control\":" + String(controlState ? "true" : "false");
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleAttendance() {
  String html = "<h3> Today's Attendance - " + getDateString() + "</h3>";
  
  if (attendanceCount == 0) {
    html += "<p>No attendance records for today.</p>";
  } else {
    for (int i = 0; i < attendanceCount; i++) {
      html += "<div class='log-entry'>";
      html += "<strong>" + attendanceRecords[i].userName + "</strong> - ";
      html += "Date: " + attendanceRecords[i].date + " - ";
      html += "Time: " + attendanceRecords[i].time;
      html += " - <span style='color: green;'>PRESENT</span>";
      html += "</div>";
    }
  }
  
  server.send(200, "text/html", html);
}

void handleExport() {
  String csv = "User ID,User Name,Date,Time,DateTime\n";
  
  for (int i = 0; i < attendanceCount; i++) {
    csv += String(attendanceRecords[i].userId) + ",";
    csv += attendanceRecords[i].userName + ",";
    csv += attendanceRecords[i].date + ",";
    csv += attendanceRecords[i].time + ",";
    csv += attendanceRecords[i].datetime + "\n";
  }
  
  server.send(200, "text/csv", csv);
  server.sendHeader("Content-Disposition", "attachment; filename=attendance_" + getDateString() + ".csv");
}

void handleUsers() {
  String json = "[";
  for (int i = 1; i <= 6; i++) {
    if (i > 1) json += ",";
    json += "{";
    json += "\"id\":" + String(users[i].id) + ",";
    json += "\"name\":\"" + users[i].name + "\",";
    json += "\"role\":\"" + users[i].role + "\",";
    json += "\"department\":\"" + users[i].department + "\",";
    json += "\"enrolled\":" + String(users[i].isEnrolled ? "true" : "false") + ",";
    json += "\"last_attendance\":\"" + users[i].lastAttendance + "\"";
    json += "}";
  }
  json += "]";
  
  server.send(200, "application/json", json);
}

void enrollUser(int userId) {
  Serial.printf("Enrolling user: %s (ID: %d)\n", users[userId].name.c_str(), userId);
  
  for(int i = 0; i < ENROLL_CONFIRM_TIMES; i++) {
    Serial.printf("Enrollment sample %d/%d for %s\n", i+1, ENROLL_CONFIRM_TIMES, users[userId].name.c_str());
    delay(1000);
  }
  
  users[userId].isEnrolled = true;
  Serial.printf("User %s enrolled successfully!\n", users[userId].name.c_str());
  
  if (controlState) {
    sendEnrollmentToFirebase(userId);
  }
}

void markAttendance(int userId) {
  if (!users[userId].isEnrolled) {
    Serial.printf("User %s is not enrolled!\n", users[userId].name.c_str());
    return;
  }
  
  String currentDate = getDateString();
  String currentTime = getTimeString();
  String currentDateTime = getDateTimeString();
  
  users[userId].lastAttendance = currentDateTime;
  
  // Store attendance record locally
  if (attendanceCount < 100) {
    attendanceRecords[attendanceCount].userId = userId;
    attendanceRecords[attendanceCount].userName = users[userId].name;
    attendanceRecords[attendanceCount].date = currentDate;
    attendanceRecords[attendanceCount].time = currentTime;
    attendanceRecords[attendanceCount].datetime = currentDateTime;
    attendanceCount++;
  }
  
  Serial.printf("Attendance marked for %s on %s at %s\n", 
                users[userId].name.c_str(), currentDate.c_str(), currentTime.c_str());
  
  if (controlState) {
    sendAttendanceToFirebase(userId, currentDate, currentTime, currentDateTime);
  }
}

void sendEnrollmentToFirebase(int userId) {
  FirebaseJson json;
  json.set("user_id", userId);
  json.set("name", users[userId].name);
  json.set("role", users[userId].role);
  json.set("department", users[userId].department);
  json.set("enrollment_time", getDateTimeString());
  
  String path = "/users/" + String(userId);
  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
    Serial.println("User enrollment sent to Firebase");
  } else {
    Serial.println("Failed to send enrollment to Firebase: " + fbdo.errorReason());
  }
}

void sendAttendanceToFirebase(int userId, String date, String time, String datetime) {
  FirebaseJson json;
  json.set("user_id", userId);
  json.set("name", users[userId].name);
  json.set("role", users[userId].role);
  json.set("department", users[userId].department);
  json.set("date", date);
  json.set("time", time);
  json.set("datetime", datetime);
  json.set("type", "FACE_RECOGNITION");
  
  String path = "/attendance/logs/" + String(millis());
  if (Firebase.RTDB.pushJSON(&fbdo, path.c_str(), &json)) {
    Serial.println("Attendance sent to Firebase");
    
    Firebase.RTDB.setString(&fbdo, "/attendance/last_attendance/name", users[userId].name.c_str());
    Firebase.RTDB.setString(&fbdo, "/attendance/last_attendance/datetime", datetime.c_str());
    
  } else {
    Serial.println("Failed to send attendance to Firebase: " + fbdo.errorReason());
  }
}

void loop() {
  server.handleClient();
  
  static unsigned long lastRecognition = 0;
  if (faceRecognitionEnabled && millis() - lastRecognition > 15000) {
    lastRecognition = millis();
    
    int recognizedFace = random(-1, 4);
    if (recognizedFace >= 1 && recognizedFace <= 6 && users[recognizedFace].isEnrolled) {
      if (attendanceEnabled && (millis() - lastAttendanceTime > ATTENDANCE_COOLDOWN)) {
        markAttendance(recognizedFace);
        lastAttendanceTime = millis();
      }
    }
  }
  
  delay(10);
}