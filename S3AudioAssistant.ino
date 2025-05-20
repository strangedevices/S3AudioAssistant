#include <BleKeyboard.h>

// All rights reserved Jeremy Kelaher 2025
// You can use this code for personal experiemenation, but if you want to use it in a product please email oni@mashplex.com

#include <Arduino.h>
#include <M5Unified.h>
#include <M5ModuleLLM.h>
#include <M5CoreS3.h>


// AI module globals
M5ModuleLLM module_llm;

// The KWS is VERY sensitive to the words used. Must be all capitalized. They have to be "longish" or words are missed.
// Multiple words are hit or miss. 
// LISTEN Works well, but is a bit common. RECORD works. COMMAND, QUESTION Fail.
// NOTHING works too well, so I have obfiscated it for now
String wake_up_keyword = "ACKNOWLEDGE";

// all the AI workers in the module ...
String kws_work_id;
String asr_work_id;

String language;
m5_module_llm::ApiLlmSetupConfig_t llmConfig;
String llm_work_id;


// AI audio out
String tts_work_id;
bool mute = false;
bool speechOn = true;
// speech streaming is a trade off - it is more stable and starts faster, but can sound more "jerky"
// if you do not use stteaming be aware that really long utternces can crash the tts so bad a restart is needed
// I have seen spontanous crashes of the LLM module when its is doing lots of TTS as well and inferencing the larger (1.5b) parameter models
bool speechStreaming = true;
// various stregied for decinging whne
// Random integer from 1 up to ditrib thrown, and if value is 1 speech is sent
#include <random>

// Create a random number generator
std::random_device speechCadenceSeed;  // Seed
// Define the range for the random integer (e.g., 1 to 100)
std::uniform_int_distribution<> speechCadenceDistribution(1, 2);
std::mt19937 speechCadenceGenerator(speechCadenceSeed()); // Mersenne Twister engine
int speechCadenceCounter = 0;
unsigned long lastSpeechTime = 0;

// define the strategy for how often a "speech unit" should be sent to TTS
// TODO clean this up once soemthing works well enough
// each speech part is about 2 words
// average in English is 120 WPM.
// so the TTS unit can "do" max one per second, 1000ms
// it stuggles to keep up is fed too fast however
// average English word is 6 charaters
bool speakNow(int bufferAtTheMoment) {
  unsigned long thisSpeechTime = millis();
  speechCadenceCounter++;
  lastSpeechTime = thisSpeechTime;
  // Approach is based on length. Short utterence will happen after inference is complete
  // if (bufferAtTheMoment>40) return true;
  // return(speechCadenceDistribution(speechCadenceGenerator)==1); // Random
  // return true; // every time, "jerky" but low latency
  return !(speechCadenceCounter%3); // every 3rd
  // return false;
}


// OK some somewhst complex code needed to convert streaming part words to things the module can actally say.
// focus is on sending a reasonable number of words in a chunk. In english, this is controlled by spaces
// and also to handle things TTS can not say or says badly, eg numbers


#include <string>
#include <vector>
#include <functional>
#include <cctype>

// the standard tts just hates many strings
// even this will not save you, but its a start :)

// "fix" decimal number for tts can say them
String ttsReplaceDecimalWithPoint(String input) {
  String result = input;
  int pos = 0;
  
  while (pos < result.length()) {
    // Find the decimal point
    pos = result.indexOf('.', pos);
    if (pos == -1) break;
    
    // Verify we have numbers before and after the decimal point
    if (pos > 0 && pos < result.length() - 1) {
      // Check if characters before and after are digits
      if (isDigit(result.charAt(pos - 1)) && isDigit(result.charAt(pos + 1))) {
        // Replace the decimal point with " point "
        result = result.substring(0, pos) + " point " + result.substring(pos + 1);
        pos += 7; // Skip past " point "
      } else {
        pos++; // Move to next character
      }
    } else {
      pos++; // Move to next character
    }
  }
  
  return result;
}

// "fix" fraction so tts can say them
String ttsReplaceFractionWithOver(String input) {
  String result = input;
  int pos = 0;
  
  while (pos < result.length()) {
    // Find the decimal point
    pos = result.indexOf('/', pos);
    if (pos == -1) break;
    
    // Verify we have numbers before and after the decimal point
    if (pos > 0 && pos < result.length() - 1) {
      // Check if characters before and after are digits
      if (isDigit(result.charAt(pos - 1)) && isDigit(result.charAt(pos + 1))) {
        // Replace the decimal point with " point "
        result = result.substring(0, pos) + " over " + result.substring(pos + 1);
        pos += 6; // Skip past " point "
      } else {
        pos++; // Move to next character
      }
    } else {
      pos++; // Move to next character
    }
  }
  
  return result;
}

// TODO merge the above properly :)
String ttsBetterNumbers (String input) {
  return(ttsReplaceDecimalWithPoint(ttsReplaceFractionWithOver(input)));
}

String ttsCleanString(const String& input) {
    String result;
    
    for (char c : input) {
      if( c == '!' || c == '?' || c == ':' || c == ';') {
        // all punctuation is . so there
        result +=".";
        continue;
      }
      if(c == ',' || c == '\'' || c == '"' || c == '`' ) {
        // commas are boring (and the LLM sometimes shoves them in numbers)
        // make all quotes go away
        result +="";
        continue;
      }
      if (std::isalnum(c) || c == ' ' || c == '.'|| c == '/' ) {
          result += c;
      } else { result +=' ';}// all other funny shit shall become spaces as many blow TTS up
    }
    // replace {number}.{number} with {number} point {number} 
    return result;
}



// ----------------------------------------------------------------------------------------------------------

static m5::touch_state_t prev_state; // touch state persistence
M5Canvas canvas(&CoreS3.Display); // Create a canvas for scrolling text area - status
M5Canvas outputCanvas(&CoreS3.Display); // Create another canvas for scrolling text area - output of STT and LLM

// soft buttons
String rootButtons =      "LISTEN    STOP!       GO     ";
String inferenceButtons = "PAUSE     STOP!       ..     ";
String modelButtons  =    "INFO       CHAT      MORE";
String modelButtons2 =    "FAST      CODE     MORE";
String modelButtons3 =    "THINK                    MORE";
void showSoftButtons(String buttonText)
{
  CoreS3.Display.setTextColor(BLACK);
  CoreS3.Display.setTextSize(0.9);
  CoreS3.Display.fillRect(0, CoreS3.Display.height() - 40, CoreS3.Display.width() / 3, 40, GREEN);
  CoreS3.Display.fillRect(CoreS3.Display.width() / 3, CoreS3.Display.height() - 40, CoreS3.Display.width() / 3, 40,
                          RED);
  CoreS3.Display.fillRect((CoreS3.Display.width() / 3) * 2, CoreS3.Display.height() - 40, CoreS3.Display.width() / 3,
                          40, BLUE);
  CoreS3.Display.drawString(buttonText, CoreS3.Display.width() / 2, CoreS3.Display.height() - 20);
}

bool isButton1(int x, int y)
{
  return ((CoreS3.Display.width() / 3 > x && x > 0) && (CoreS3.Display.height() > y && y > CoreS3.Display.height() - 40) );
}

bool isButton2(int x, int y)
{
  return (((CoreS3.Display.width() / 3) * 2 > x && x > CoreS3.Display.width() / 3) && (CoreS3.Display.height() > y && y > CoreS3.Display.height() - 40) );
}

bool isButton3(int x, int y)
{
  return ((CoreS3.Display.width() > x && x > (CoreS3.Display.width() / 3) * 2) && (CoreS3.Display.height() > y && y > CoreS3.Display.height() - 40) );
}

void resetAI(String model)
{
    // soft reset of LLM module
    /* Make sure module is connected */
    canvas.println("Check AI subprocessor 1 available...");
    canvas.pushSprite(0, 0);
    while (1) {
        if (module_llm.checkConnection()) {
            break;
        }
    }

    /* Reset ModuleLLM */
    canvas.println("Reset AI subprocessor 1...");
    canvas.pushSprite(0, 0);
    module_llm.sys.reset();

    /* Setup Audio module */
    canvas.println("Setup audio...");
    canvas.pushSprite(0, 0);
    module_llm.audio.setup();

    /* Setup KWS module and save returned work id */
    canvas.println("Setup wake word...");
    canvas.pushSprite(0, 0);
    m5_module_llm::ApiKwsSetupConfig_t kws_config;
    kws_config.kws = wake_up_keyword;
    // sadly there is no way from setup to turn off the annoying "acknoledgement word" - eg json doc["data"]["enwake_audio"] = false
    // I tried a work around, but it failed
    kws_work_id    = module_llm.kws.setup(kws_config, "kws_setup", language);
    // hate keywords ? The just Serial2.printf("{ \"request_id\":\"1\",\"work_id\":\"%s\",\"action\":\"exit\" }", kws_work_id);
    // Disarm KWS until Listen pressed once :) 
    // Serial2.printf("{ \"request_id\":\"1\",\"work_id\":\"%s\",\"action\":\"pause\" }", kws_work_id);

    /* Setup ASR module and save returned work id */
    canvas.println("Setup speech recognition...");
    canvas.pushSprite(0, 0);
    m5_module_llm::ApiAsrSetupConfig_t asr_config;
    asr_config.input = {"sys.pcm", kws_work_id};
    asr_work_id      = module_llm.asr.setup(asr_config, "asr_setup", language);
  

    // Boot up the LLM
    llmConfig.max_token_len=1000;
    llmConfig.prompt="You are a helpful assitant spelt Oni, pronounced Onwy. You were created by Jeremy Kelaher. Make your answers as brief as you can while still being accurate. Don't overthink. Do a first guess. Check if you are unsure only.";
    llmConfig.model=model.c_str();
    canvas.printf("boot %s context %d...\n", llmConfig.model.c_str(), llmConfig.max_token_len);
    canvas.pushSprite(0, 0);
    llm_work_id = module_llm.llm.setup(llmConfig);
    
    // BOOT Text to Speech
    canvas.println("boot voice ...");
    m5_module_llm::ApiTtsSetupConfig_t tts_config;
    tts_work_id = module_llm.tts.setup(tts_config, "tts_setup", language);
}

// best source for model names once you upgrade the LLM module is https://github.com/m5stack/StackFlow/tree/dev/projects/llm_framework/main_llm/models
    // "think fast" (note really screws up sometimes)
    // llmConfig.model="qwen2.5-0.5B-prefill-20e";
    // "think well" good default ?
    // llmConfig.model="qwen2.5-1.5B-ax630c";
    // "let's chat"
    // llmConfig.model="openbuddy-llama3.2-1B-ax630c"; 
    // "think hard" (very slow)
    // llmConfig.model="deepseek-r1-1.5B-ax630c"; // TODO for this model handle <think> and </think> for summary and for TTS etc
void chooseAI()
{
  while(1) {
    // bank one
    showSoftButtons(modelButtons);
    outputCanvas.println("Chose my thinking style");
    outputCanvas.pushSprite(0, (CoreS3.Display.height() - 40)/3);
    while(1) {
      CoreS3.update();
      auto touchPoint = CoreS3.Touch.getDetail();
      if (prev_state != touchPoint.state) {
          prev_state = touchPoint.state;
      }
      if (touchPoint.state == m5::touch_state_t::touch_begin) {
          if(isButton1(touchPoint.x,touchPoint.y)) {
            // INFO model
            resetAI("qwen2.5-1.5B-ax630c");
            return;
          }
          if(isButton2(touchPoint.x,touchPoint.y)) {
            // CHAT model
            resetAI("openbuddy-llama3.2-1B-ax630c");
            return;
          }
          if(isButton3(touchPoint.x,touchPoint.y)) {
            // more models !
            break;
          }
      }
      delay(100);
    }
    // bank 2
    showSoftButtons(modelButtons2);
    while(1) {
      CoreS3.update();
      auto touchPoint = CoreS3.Touch.getDetail();
      if (prev_state != touchPoint.state) {
          prev_state = touchPoint.state;
      }
      if (touchPoint.state == m5::touch_state_t::touch_begin) {
          if(isButton1(touchPoint.x,touchPoint.y)) {
            // FAST model
            resetAI("qwen2.5-0.5B-prefill-20e");
            return;
          }
          if(isButton2(touchPoint.x,touchPoint.y)) {
            // CODE model
            resetAI("qwen2.5-1.5B-ax630c"); //TODO update once a larger qwen coder model is available
            return;
          }
          if(isButton3(touchPoint.x,touchPoint.y)) {
            // more models !
            break;
          }
      }
      delay(100);
    }
    // last bank
    showSoftButtons(modelButtons3);
    while(1) {
      CoreS3.update();
      auto touchPoint = CoreS3.Touch.getDetail();
      if (prev_state != touchPoint.state) {
          prev_state = touchPoint.state;
      }
      if (touchPoint.state == m5::touch_state_t::touch_begin) {
          if(isButton1(touchPoint.x,touchPoint.y)) {
            // THINK model
            resetAI("qwen2.5-1.5B-ax630c");
            return;
          }
          if(isButton2(touchPoint.x,touchPoint.y)) {
            // SPARE model
          }
          if(isButton3(touchPoint.x,touchPoint.y)) {
            // back to start
            break;
          }
      }
      delay(100);
    }
  }
}

void setup()
{
  try {
    //M5.begin();
    auto cfg = M5.config();
    CoreS3.begin(cfg);

    // TODO read back in some config things from flash

    //CoreS3.Display.setTextColor(RED);
    CoreS3.Display.setFont(&fonts::Orbitron_Light_24);
    // CoreS3.Display.setTextSize(5);
    CoreS3.Display.setTextDatum(middle_center);
    //CoreS3.Display.drawString("^ONI^", CoreS3.Display.width() / 2, 55);
    // divider
    CoreS3.Display.drawLine(0, (CoreS3.Display.height() - 40)/3-1 , CoreS3.Display.width(), (CoreS3.Display.height() - 40)/3-1 , ORANGE);  // Orange color
    CoreS3.update();
    

    // Set up status canvas (smaller than screen to allow scrolling)
    canvas.createSprite(CoreS3.Display.width(), ((CoreS3.Display.height() - 40)/3)-3); 
    canvas.setTextScroll(true); // Enable text scrolling (if supported by M5GFX)
    canvas.setFont(&fonts::Orbitron_Light_24);
    canvas.setTextSize(0.5);
    canvas.println("^ONI^ is booting ...");
    canvas.pushSprite(0, 0);
    // Set up output canvas (smaller than screen to allow scrolling)
    // TTS and LLM will show here, its the "reading screen"
    outputCanvas.createSprite(CoreS3.Display.width(), (CoreS3.Display.height() - 40)/3*2); // Exclude bottom 40px where soft buttons are and then 2/3
    outputCanvas.setTextScroll(true); // Enable text scrolling (if supported by M5GFX)
    outputCanvas.setFont(&fonts::Orbitron_Light_24);
    outputCanvas.setTextSize(0.66);
    outputCanvas.println("I am Oni");
    outputCanvas.pushSprite(0, (CoreS3.Display.height() - 40)/3);
    // M5.Display.setFont(&fonts::efontCN_12);  // Support Chinese display

    language = "en_US";
    // language = "zh_CN";

    /* Init module serial port */
    // int rxd = 16, txd = 17;  // Basic
    // int rxd = 13, txd = 14;  // Core2
    // int rxd = 18, txd = 17;  // CoreS3
    int rxd = M5.getPin(m5::pin_name_t::port_c_rxd);
    int txd = M5.getPin(m5::pin_name_t::port_c_txd);
    Serial2.begin(115200, SERIAL_8N1, rxd, txd);

    /* Init module */
    module_llm.begin(&Serial2);


    // resetAI("qwen2.5-1.5B-ax630c");
    // resetAI("deepseek-r1-1.5B-ax630c");
    chooseAI();
    // delay(500);
    module_llm.tts.inference(tts_work_id, "Owny is awake", 0);
    
    
    canvas.printf("^ONI^ ok\npress LISTEN. You can also say \"%s\"\n", wake_up_keyword.c_str());
    canvas.pushSprite(0, 0);

    CoreS3.Speaker.setVolume(100);  // Set volume 
    CoreS3.Speaker.tone(262, 300);  // C4
    delay(200);                     // Slight pause between notes
    CoreS3.Speaker.tone(294, 300);  // D4
    delay(200);                     // Slight pause between notes
    CoreS3.Speaker.tone(262, 300);  // C4
    // setup basic touch zones last as they will not work until loop() is entered
    showSoftButtons(rootButtons);
    // throw std::runtime_error("Test error: something went badly wrong!");
  } catch (const std::runtime_error& e) {
    // Handle std::runtime_error specifically
    // std::cerr << "Runtime error: " << e.what() << std::endl;
    canvas.setTextColor(TFT_ORANGE);
    canvas.printf("->RUNTIME EXCEPTION %s RESETTING\n", e.what());
    canvas.pushSprite(0, 0);
    delay(5000);
    ESP.restart();
  } catch (const std::exception& e) {
    // Handle other standard exceptions
    // std::cerr << "Standard exception: " << e.what() << std::endl;
    canvas.setTextColor(TFT_ORANGE);
    canvas.printf("->STD EXCEPTION %s RESETTING\n", e.what());
    canvas.pushSprite(0, 0);
    delay(5000);
    ESP.restart();
  } catch (...) {
    // something random went wrong !
    // std::cerr << "Unknown exception caught" << std::endl;
    canvas.setTextColor(TFT_RED);
    canvas.printf("->UNKNOWN EXCEPTION RESETTING\n");
    canvas.pushSprite(0, 0);
    delay(5000);
    ESP.restart();
  }
}

String utterence = "";
String question = "";
String thisResponse="";
String lastQuestion= "";
String lastResponse= "";
int speakFrom= 0; // index into thisResponse for tts

// precompile the voice command regexp
#include <regex>
std::regex muteCommand(R"(.*mute go\.$)");
std::regex unmuteCommand(R"(.*unmute go\.$)");
std::regex batteryCommand(R"(.*battery charge go\.$)");
std::regex goCommand(R"(.* go\.$)");

// precompile some token processing regexp

std::regex thinkingToken("<think>");
std::regex stopThinkingToken("</think>");
bool LLMisThinking = false; // used when thinking tokens are detected to prevent excessive resposnes to user and storage of workings

// stats
int batteryState=0; // %
bool charging=false;
int USBVoltage=0; // in mV

// inference output state control
bool skipOutput=false;
// go mode causes the LLM to be invoked as soon as a full stop is seen. uttering go at the end of a sentence turns it on for one command
// go mode is intended where hands free is imperative
bool goMode=false;

// Sketch > Include Library > Add Zip Library
// Search for BleKeyboard (by T-vK or other variants).
// https://github.com/T-vK/ESP32-BLE-Keyboard
#include <BleKeyboard.h>
#define USE_NIMBLE
BleKeyboard bleKeyboard("ONI Keyboard", "Microsoft", 100); // Initial battery level (100%)
//BleKeyboard bleKeyboard;
bool BleKBConnected = false;

void loop()
{
  try {
    /* Update ModuleLLM */
    module_llm.update();
    CoreS3.update();

    // get key stats
    batteryState=CoreS3.Power.getBatteryLevel();
    charging=CoreS3.Power.isCharging();
    USBVoltage=CoreS3.Power.getVBUSVoltage();

    // handle touch zones and "Power"button
    auto touchPoint = CoreS3.Touch.getDetail();
    if (prev_state != touchPoint.state) {
        prev_state = touchPoint.state;
    }
    if ((touchPoint.state == m5::touch_state_t::touch_begin)||goMode) {
        // CoreS3.Display.fillRect(0, 40, CoreS3.Display.width(), 70, BLACK);
        if((isButton1(touchPoint.x,touchPoint.y))&&(!goMode)){
          // Listen
          utterence = ""; // new utterence
          CoreS3.Speaker.tone(262, 300);  // C4
          canvas.setTextColor(TFT_GREEN);
          canvas.println("Listening...");
          canvas.pushSprite(0, 0);
          Serial2.printf("{ \"request_id\":\"1\",\"work_id\":\"%s\",\"action\":\"work\" }", kws_work_id); // also enable use of trigger word for future use
          Serial2.printf("{ \"request_id\":\"3\",\"work_id\":\"%s\",\"action\":\"work\" }", asr_work_id); // enable asr now
          delay(200);                     // Slight pause between notes
          CoreS3.Speaker.tone(294, 300);  // D4
          delay(200);                     // Slight pause between notes
          CoreS3.Speaker.tone(330, 300);  // E4
        }
        if((isButton2(touchPoint.x,touchPoint.y))&&(!goMode)){
          CoreS3.Speaker.tone(330, 300);  // E4 
          // STOP
          canvas.setTextColor(TFT_RED);
          canvas.println("\nStopping recognition and starting new chat. Trigger word enabled.");
          canvas.pushSprite(0, 0);
          utterence = "";
          question = "";
          lastQuestion= "";
          lastResponse= "";
          // TODO store old chat sessions
          // restart KWS
          // Serial2.printf("{ \"request_id\":\"1\",\"work_id\":\"%s\",\"action\":\"work\" }", kws_work_id); // seems to a good idea ...
          // CoreS3.Speaker.tone(330, 300);  // E4 
          delay(200);                     // Slight pause between notes
          CoreS3.Speaker.tone(294, 300);  // D4
          delay(200);                     // Slight pause between notes
          CoreS3.Speaker.tone(262, 300);  // C4
        }
        if((isButton3(touchPoint.x,touchPoint.y)) || goMode ){
          // stop just KWS :) This stops the AI hearing its own trigger words.
          Serial2.printf("{ \"request_id\":\"1\",\"work_id\":\"%s\",\"action\":\"pause\" }", kws_work_id);
          outputCanvas.setTextColor(TFT_WHITE);
          outputCanvas.println(""); // get ready for output
          outputCanvas.pushSprite(0, (CoreS3.Display.height() - 40)/3);
          goMode=false; // clear this before we forget
          CoreS3.Speaker.tone(262, 300);  // C4
          // Go means its LLM time
          canvas.setTextColor(TFT_BLUE);
          canvas.printf("\n%s GO\n", llmConfig.model.c_str());
          canvas.pushSprite(0, 0);
          String fullQuestion = "";
          // send inference to LLM
          lastQuestion=question;
          question=utterence;
           if (lastResponse.length()>0){
            // there is past context
            // fullQuestion="Prevously you answered: \""+lastResponse+"\" to the question:\n \""+lastQuestion+"\"\nThe new question is: "+question;
            fullQuestion="Fact:"+lastResponse+"\nQuestion:"+question;
            canvas.println(fullQuestion.c_str());
            canvas.pushSprite(0, 0);
            //delay(10000);
          } else {
            fullQuestion=question; // first "shot" - either STOP has been used or just after boot
          }
          outputCanvas.setTextColor(TFT_WHITE);
          // engineer context
          // Push question to LLM module and wait inference results
          thisResponse="";
          speakFrom=0;
          showSoftButtons(inferenceButtons);
          skipOutput=false;
          Serial2.printf("{ \"request_id\":\"42\",\"work_id\":\"%s\",\"action\":\"work\" }", tts_work_id); // attempt unshitting
          module_llm.llm.inferenceAndWaitResult(llm_work_id, fullQuestion.c_str(), [](String& result) {
            if(result=="null") { // odd case I have seen, seems to not be recoverable ? model did not load right
              canvas.setTextColor(TFT_RED);
              canvas.printf("->AI subprocessor fail, rebooting\n");
              canvas.pushSprite(0, 0);
              delay(5000);
              ESP.restart();
            }
            if(skipOutput) return; // fake stop just silently ignores the remaining results
            // detect thinking. Note this assumes that this apprears in teh scope of just one result
             if (std::regex_search(result.c_str(), thinkingToken)) {
              LLMisThinking = true;
              if(!mute) module_llm.tts.inference(tts_work_id, "hmmm let me think!", 0);
              canvas.setTextColor(TFT_YELLOW);
              canvas.print("");
              canvas.pushSprite(0, 0);
              CoreS3.update();
             } else if (std::regex_search(result.c_str(), stopThinkingToken)) {
              LLMisThinking = false;
             }
            // Show result on screen
            // if LLM this thinking send to status, if not send to main and do not "remember"
            if(LLMisThinking) {
              canvas.print(result.c_str());
              canvas.pushSprite(0, 0);
            } else {
              outputCanvas.print(result.c_str());
              thisResponse+=result;
              outputCanvas.pushSprite(0, (CoreS3.Display.height() - 40)/3);
            }
            CoreS3.update(); // so UX events can be interpreted
            // handle soft buttons
            auto inferenceTouchPoint = CoreS3.Touch.getDetail();
            if (prev_state != inferenceTouchPoint.state) {
                prev_state = inferenceTouchPoint.state;
            }
            if (inferenceTouchPoint.state == m5::touch_state_t::touch_begin) {
              if(isButton1(inferenceTouchPoint.x,inferenceTouchPoint.y)){
                CoreS3.Speaker.tone(294, 100);  // D4
                delay(2000);
                CoreS3.Speaker.tone(294, 100);  // D4
              }
              if(isButton2(inferenceTouchPoint.x,inferenceTouchPoint.y)){
                CoreS3.Speaker.tone(294, 100);  // D4
                skipOutput=true; // fake stop immediately
                // try for a real stop :) may have some lag
                Serial2.printf("{ \"request_id\":\"4\",\"work_id\":\"%s\",\"action\":\"pause\" }", llm_work_id);
                // note this technique can be resumed with "work" - may be worth trying this for pause
                // stop any speaking - note this is pretty ineffective as once something is sent to say it gets said.
                // Serial2.printf("{ \"request_id\":\"10\",\"work_id\":\"%s\",\"action\":\"pause\" }", tts_work_id);
                return;
              } 
              if(isButton3(inferenceTouchPoint.x,inferenceTouchPoint.y)){
                // does nothing for now, reserved to "resume" is pause is implemented using pause/work
              }
            } 
            // streaming Text to speech
            // this is complicated by the fact that the LLM streams send framents, partically of numbers, and the fact the TTS engine can only say some things
            // properly if in context of other words
            // what follows is an attempt to deal with this, but its not perfect and maybe only works in English at this time
            // speechStreaming controls this mode
            if(speechOn&&!mute&&speechStreaming&&!LLMisThinking) {
              // look for a space at the beginning or end of the most recent string, if not we will just keep on keeping on
              int endIndex = thisResponse.length()-1;
              char lastChar = result.charAt(result.length()-1);
              char firstChar = result.charAt(0);
              String toSay = "";
              if((firstChar==' ')&&speakNow(endIndex-speakFrom)) { // the speakNow delays this sometimes maybe endIndex-speakFrom is how big of a buffer we have accumulated
                // make sure to say what we have up to now
                int newSpeakFrom=endIndex-result.length()+1;
                toSay = ttsCleanString(thisResponse.substring(speakFrom,newSpeakFrom));
                speakFrom=newSpeakFrom;
              }
              //if((lastChar=='.')&&speakNow(endIndex-speakFrom)&&(endIndex>=0)) {
              //  toSay+=ttsCleanString(thisResponse.substring(speakFrom,endIndex));
              //  speakFrom=endIndex;
              //}
              if (toSay.length()>0){
                String finalToSay = ttsBetterNumbers(toSay);
                //canvas.printf("|%s|", finalToSay.c_str());
                //canvas.pushSprite(0, 0);
                module_llm.tts.inference(tts_work_id, finalToSay, 0);
              }
            }
          });
          LLMisThinking = false; // even if it was thinking, its not now ;)
          if(speechOn&&!mute&&speechStreaming) {
            //if(speakFrom<thisResponse.length()-2){
              String toSay=ttsBetterNumbers(ttsCleanString(thisResponse.substring(speakFrom,thisResponse.length()-2))); // skip last char, for "reasons" and "fix" any numbers
              //canvas.printf("|%s|", toSay.c_str());
              //canvas.pushSprite(0, 0);
              module_llm.tts.inference(tts_work_id, toSay, 0);
            //}
          } // flush any final things to say if using streaming tts
          showSoftButtons(rootButtons);
          // TODO try proper memory strategies optimised for smaller context windows !

          // TODO fix this ugly hack
          String cleanIt;
          if(thisResponse.length()>4) {
            cleanIt = ttsCleanString(thisResponse.substring(0,thisResponse.length()-2)); // why skip last char ? no idea ATM, but if you do not TTS and maybe future inference shits itself
          } else {
            cleanIt = ttsCleanString(thisResponse);
          }
          lastResponse= cleanIt.substring(0, 200); // make sure the stored response it not too long and is clean
          if(speechOn&&!mute&&!speechStreaming) {
            // non streaming tests to speech
            // not this may sound better for many utterences
            //outputCanvas.println("Saying: "+toSay);
            //outputCanvas.pushSprite(0, (CoreS3.Display.height() - 40)/3);
            module_llm.tts.inference(tts_work_id, cleanIt, 0);
          }
          outputCanvas.println("");
          outputCanvas.pushSprite(0, (CoreS3.Display.height() - 40)/3);
          
          CoreS3.Speaker.tone(262, 300);  // C4
        }
    }
   
    // "power" button is used to turn on bluetooth advertisements, and next tap turns it off if not yet connected
    // once connected pressing the button sends the last LLM result as typing
    // TODO create a seral port too, and maybe more things :)
    int state = CoreS3.BtnPWR.wasClicked();
    if (state) {
      // BT Keyboard out
      if(BleKBConnected) {
        // send last LLM result
         if (bleKeyboard.isConnected()) {
          // iterate slowish through string. Fast sending does not work too well !
          for (char c : thisResponse) {
            bleKeyboard.print(c); // Send text
            delay(25);
          }
          // bleKeyboard.print(thisResponse.c_str()); // Send text
          //bleKeyboard.setBatteryLevel(batteryState); // Update BLE HID battery report for fun
         } else {
          CoreS3.Speaker.tone(294, 300);  // D4
          canvas.setTextColor(TFT_ORANGE);
          canvas.println("BT Keyboard Advertising OFF");
          canvas.pushSprite(0, 0);
          bleKeyboard.end();
          BleKBConnected=false;
         }
      } else {
        CoreS3.Speaker.tone(294, 300);  // D4
        canvas.setTextColor(TFT_ORANGE);
        canvas.println("BT Keyboard Advertising ON");
        canvas.pushSprite(0, 0);
        bleKeyboard.begin();
        BleKBConnected=true; // maybe ;)
      }
    }

    /* Handle module response messages */
    for (auto& msg : module_llm.msg.responseMsgList) {
        /* If KWS module message */
        if (msg.work_id == kws_work_id) {
          // TODO maybe use some work ids so this can say some helpful things
            //canvas.setTextColor(TFT_GREEN);
            //canvas.println("Listening!");
            //canvas.pushSprite(0, 0);
            utterence = ""; // new utterence
            //CoreS3.Speaker.tone(262, 500);  // C4
            continue;
        }

        /* If ASR module message */
        if (msg.work_id == asr_work_id) {
            /* Check message object type */
            if (msg.object == "asr.utf-8.stream") {
                /* Parse message json and get ASR result */
                // odd thing is the "delta" are not delta ... they are full copies !
                // there is no easy way to tell when ASR is done as the "full stop" only happens if timeout is not reached
                // TODO maybe do something about that ? Not a big deal ATM as you can press "go" whenever you like, but to autmatticaly kick off GO
                // that would not work
                JsonDocument doc;
                DeserializationError JSONError = deserializeJson(doc, msg.raw_msg);
                if (JSONError == DeserializationError::Ok){ // more error han8dling jezza, serously mate
                  String lastUtterence = utterence; // will be "" if this is first packet
                  utterence = ttsCleanString(doc["data"]["delta"].as<String>()); // stash in the global
                  uint asr_index = doc["data"]["index"].as<uint>();

                  outputCanvas.setTextColor(TFT_YELLOW);
                  outputCanvas.printf("%s",(utterence.substring(lastUtterence.length())).c_str()); // print just the difference is any
                  outputCanvas.pushSprite(0, (CoreS3.Display.height() - 40)/3);
                }
            } else {
                canvas.setTextColor(TFT_RED);
                //canvas.printf(">>ASR %s recieved\n",msg.object.c_str());
                canvas.printf(">>Acknoledge\n");
                canvas.pushSprite(0, 0);
            }
        }

        // check for voice commands
        // voice commands are special words at the end of an utterance
        if (std::regex_match(utterence.c_str(), unmuteCommand)) {
          mute=false; // active TTS
          CoreS3.Speaker.setVolume(100);  // Set UX volume
          canvas.setTextColor(TFT_RED);
          canvas.printf("->UNMUTE\n");
          canvas.pushSprite(0, 0); 
          utterence="";
        } else if (std::regex_match(utterence.c_str(), muteCommand)) {
          mute=true; // will deactive TTS
          CoreS3.Speaker.setVolume(0);  // Set UX volume 
          canvas.setTextColor(TFT_RED);
          canvas.printf("->MUTE\n");
          canvas.pushSprite(0, 0);
          utterence="";
        } else if (std::regex_match(utterence.c_str(), batteryCommand)) {
          canvas.setTextColor(batteryState<50?TFT_RED:TFT_GREEN);
          canvas.printf("%d\n", batteryState);
          canvas.pushSprite(0, 0);
          utterence="";
        } else if (std::regex_match(utterence.c_str(), goCommand)) {
          goMode=true;
          // TODO remove the go from the utterence, though it seem harmless ?
        }
        

    }
    /* Clear handled messages */
    module_llm.msg.responseMsgList.clear();
  } catch (const std::runtime_error& e) {
    // Handle std::runtime_error specifically
    // std::cerr << "Runtime error: " << e.what() << std::endl;
    canvas.setTextColor(TFT_ORANGE);
    canvas.printf("->RUNTIME EXCEPTION %s\n", e.what());
    canvas.pushSprite(0, 0);
  } catch (const std::exception& e) {
    // Handle other standard exceptions
    // std::cerr << "Standard exception: " << e.what() << std::endl;
    canvas.setTextColor(TFT_ORANGE);
    canvas.printf("->STD EXCEPTION %s\n", e.what());
    canvas.pushSprite(0, 0);
  } catch (...) {
    // something random went wrong !
    // std::cerr << "Unknown exception caught" << std::endl;
    canvas.setTextColor(TFT_RED);
    canvas.printf("->UNKNOWN EXCEPTION\n");
    canvas.pushSprite(0, 0);
  }
}