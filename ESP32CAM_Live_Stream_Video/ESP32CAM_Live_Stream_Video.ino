#include "esp_camera.h"
#include "WiFi.h"
#include "ESPAsyncWebServer.h"

const char *ssid = "Prateek";
const char *password = "12345@#12345";

AsyncWebServer server(80);

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32-CAM Live Stream</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            background: #f0f0f0;
            margin: 0;
            padding: 20px;
        }
        .container {
            background: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            max-width: 800px;
            margin: 0 auto;
        }
        h1 {
            color: #333;
            margin-bottom: 20px;
        }
        .video-container {
            background: black;
            border-radius: 5px;
            padding: 5px;
            margin: 10px 0;
        }
        #stream {
            width: 100%;
            max-width: 640px;
            border-radius: 5px;
        }
        .status {
            margin: 10px 0;
            padding: 10px;
            background: #e7f3ff;
            border-radius: 5px;
            color: #0066cc;
        }
        .controls {
            margin: 15px 0;
        }
        button {
            background: #007bff;
            color: white;
            border: none;
            padding: 10px 20px;
            margin: 5px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 16px;
        }
        button:hover {
            background: #0056b3;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP32-CAM Live Video Stream</h1>
        
        <div class="status" id="status">
            Connected - Streaming Live Video
        </div>
        
        <div class="video-container">
            <img src="/stream" id="stream" alt="Live Video Stream">
        </div>
        
        <div class="controls">
            <button onclick="refreshStream()">Refresh Stream</button>
            <button onclick="toggleFullscreen()">Fullscreen</button>
        </div>
        
        <div>
            <p>Stream will auto-refresh every 2 seconds</p>
        </div>
    </div>

    <script>
        // Auto-refresh the stream every 2 seconds to prevent freezing
        setInterval(function() {
            refreshStream();
        }, 2000);

        function refreshStream() {
            const stream = document.getElementById('stream');
            const timestamp = new Date().getTime();
            stream.src = '/stream?t=' + timestamp;
            
            const status = document.getElementById('status');
            status.innerHTML = 'Last refresh: ' + new Date().toLocaleTimeString();
        }

        function toggleFullscreen() {
            const stream = document.getElementById('stream');
            if (!document.fullscreenElement) {
                if (stream.requestFullscreen) {
                    stream.requestFullscreen();
                }
            } else {
                if (document.exitFullscreen) {
                    document.exitFullscreen();
                }
            }
        }

        // Handle stream loading errors
        document.getElementById('stream').onerror = function() {
            document.getElementById('status').innerHTML = 'Stream Error - Trying to reconnect...';
            setTimeout(refreshStream, 1000);
        };

        // Initial load
        window.onload = function() {
            refreshStream();
        };
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("ESP32-CAM Live Stream Starting...");

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("Camera Stream URL: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connection failed!");
    return;
  }

  // Camera configuration
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Configure image quality based on available memory
  if (psramFound()) {
    Serial.println("PSRAM found - using higher quality");
    config.frame_size = FRAMESIZE_SVGA;  // 800x600
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    Serial.println("No PSRAM - using lower quality");
    config.frame_size = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  Serial.println("Camera initialized successfully");

  // Route for root page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  // Route for video stream
  server.on("/stream", HTTP_GET, [](AsyncWebServerRequest *request) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      request->send(500, "text/plain", "Camera capture failed");
      return;
    }

    // Send the image as JPEG
    request->send_P(200, "image/jpeg", fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });

  // Start server
  server.begin();
  Serial.println("HTTP server started");
  Serial.println("Open your browser and visit: http://" + WiFi.localIP().toString());
}

void loop() {
  // Nothing to do here - the server handles everything asynchronously
  // delay(700);
}