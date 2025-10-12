

#include "esp_camera.h"
#include "FS.h"
#include "SPI.h"
#include "SD.h"
#include "EEPROM.h"
#include "driver/rtc_io.h"
#include "ESP32_MailClient.h"

// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER

#include "camera_pins.h"

#define ID_ADDRESS 0x00
#define COUNT_ADDRESS 0x01
#define ID_BYTE 0xAA
#define EEPROM_SIZE 0x0F

uint16_t nextImageNumber = 0;

#define WIFI_SSID "justDo"
#define WIFI_PASSWORD "pratik123"

#define emailSenderAccount "xxxxxxxx@gmail.com"
#define emailSenderPassword "xxxxxxxxxxxxxxxxx"
//To use send Email for Gmail to port 465 (SSL),
//less secure app option should be enabled.
//https://myaccount.google.com/lesssecureapps?pli=1

#define emailRecipient "xxxxxxx@gmail.com"
#define emailRecipient2 "xxxxx"
#define emailRecipient3 "xxxxx"
#define emailRecipient4 "xxxxxxx"

//The Email Sending data object contains config and data to send
SMTPData smtpData;

//Callback function to get the Email sending status
void sendCallback(SendStatus info);

void setup() {
  //Serial.begin(115200);
  Serial.begin(9600);
  Serial.println();
  Serial.println("Booting...");

  pinMode(4, INPUT);  //GPIO for LED flash
  digitalWrite(4, LOW);
  rtc_gpio_hold_dis(GPIO_NUM_4);  //diable pin hold if it was enabled before sleeping

  //connect to WiFi network
  Serial.print("Connecting to AP");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(200);
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();

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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  //init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  //initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  //set the camera parameters
  sensor_t *s = esp_camera_sensor_get();
  s->set_contrast(s, 2);    //min=-2, max=2
  s->set_brightness(s, 2);  //min=-2, max=2
  s->set_saturation(s, 2);  //min=-2, max=2
  delay(100);               //wait a little for settings to take effect

  //mount SD card
  Serial.println("Mounting SD Card...");
  MailClient.sdBegin(14, 2, 15, 13);

  if (!SD.begin()) {
    Serial.println("Card Mount Failed");
    return;
  }

  //initialize EEPROM & get file number
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Failed to initialise EEPROM");
    Serial.println("Exiting now");
    while (1)
      ;
  }


  if (EEPROM.read(ID_ADDRESS) != ID_BYTE) {
    Serial.println("Initializing ID byte & restarting picture count");
    nextImageNumber = 0;
    EEPROM.write(ID_ADDRESS, ID_BYTE);
    EEPROM.commit();
  } else {
    EEPROM.get(COUNT_ADDRESS, nextImageNumber);
    nextImageNumber += 1;
    Serial.print("Next image number:");
    Serial.println(nextImageNumber);
  }

  //take new image
  camera_fb_t *fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    Serial.println("Exiting now");
    while (1)
      ;
  }

  //save to SD card
  String path = "/IMG" + String(nextImageNumber) + ".jpg";
  fs::FS &fs = SD;
  File file = fs.open(path.c_str(), FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create file");
    Serial.println("Exiting now");
    while (1)
      ;
  } else {
    file.write(fb->buf, fb->len);
    EEPROM.put(COUNT_ADDRESS, nextImageNumber);
    EEPROM.commit();
  }
  file.close();
  esp_camera_fb_return(fb);
  Serial.printf("Image saved: %s\n", path.c_str());

  //send email
  Serial.println("Sending email...");
  smtpData.setLogin("smtp.gmail.com", 465, emailSenderAccount, emailSenderPassword);
  smtpData.setSender("ESP32-CAM", emailSenderAccount);

  //Set Email priority or importance High, Normal, Low or 1 to 5 (1 is highest)
  smtpData.setPriority("Normal");
  smtpData.setSubject("Alert - women safety");
  smtpData.setMessage("<div style=\"color:#003366;font-size:20px;\">Location :- https://maps.app.goo.gl/jpygp8fiz96av9em8 </div>", true);
  smtpData.addRecipient(emailRecipient);
  smtpData.addRecipient(emailRecipient2);
  smtpData.addRecipient(emailRecipient3);
  smtpData.addRecipient(emailRecipient4);

  smtpData.addAttachFile(path);
  smtpData.setFileStorageType(MailClientStorageType::SD);

  smtpData.setSendCallback(sendCallback);
  if (!MailClient.sendMail(smtpData))
    Serial.println("Error sending Email, " + MailClient.smtpErrorReason());

  //Clear all data from Email object to free memory
  smtpData.empty();

  pinMode(4, OUTPUT);            //GPIO for LED flash
  digitalWrite(4, LOW);          //turn OFF flash LED
  rtc_gpio_hold_en(GPIO_NUM_4);  //make sure flash is held LOW in sleep
  Serial.println("Entering deep sleep mode");
  Serial.flush();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);  //wake up when pin 13 goes LOW
  delay(10000);                                  //wait for 10 seconds to let PIR sensor settle
  esp_deep_sleep_start();
}

void loop() {
}

void sendCallback(SendStatus msg) {
  Serial.println(msg.info());
  if (msg.success()) {
    Serial.println("----------------");
    Serial.println("ATD+91xxxxxxxxx;");
    delay(400);
    Serial.println("AT+CMGF=1");
    delay(1000);
    Serial.println("AT+CMGS=\"+91xxxxxxxx\"\r");
    Serial.println("women safety");
    Serial.println((char)26);
    delay(50000);
  }
}
