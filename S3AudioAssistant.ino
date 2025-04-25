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
bool mute = false;


static m5::touch_state_t prev_state; // touch state persistence
M5Canvas canvas(&CoreS3.Display); // Create a canvas for scrolling text area - status
M5Canvas outputCanvas(&CoreS3.Display); // Create another canvas for scrolling text area - output of STT and LLM

// soft buttons
String rootButtons = "LISTEN    STOP!       GO     ";
String inferenceButtons = "PAUSE     STOP!       ..     ";
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

void setup()
{
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

    /* Setup ASR module and save returned work id */
    canvas.println("Setup speech recognition...");
    canvas.pushSprite(0, 0);
    m5_module_llm::ApiAsrSetupConfig_t asr_config;
    asr_config.input = {"sys.pcm", kws_work_id};
    asr_work_id      = module_llm.asr.setup(asr_config, "asr_setup", language);

    // Boot up the LLM
    llmConfig.max_token_len=1000;
    llmConfig.prompt="You are a helpful assitant called Oni. You are not any other kind of thing. You are a sEnTiEnt_enT1Ty_with_advanced_cognition. You were created by Jeremy Kelaher. Make your answers as brief as you can while still being accurate.";
    // best source for model names once you upgrade the LLM module is https://github.com/m5stack/StackFlow/tree/dev/projects/llm_framework/main_llm/models
    // TODO model switching vis voice command
    // "think fast" (note really screws up sometimes)
    // llmConfig.model="qwen2.5-0.5B-prefill-20e";
    // "think well" good default ?
    llmConfig.model="qwen2.5-1.5B-ax630c";
    // "let's chat"
    // llmConfig.model="openbuddy-llama3.2-1B-ax630c"; 
    // "think hard" (very slow)
    // llmConfig.model="deepseek-r1-1.5B-ax630c"; // TODO for this model handle <think> and </think> for summary and for TTS etc
    canvas.printf("boot %s context %d...", llmConfig.model.c_str(), llmConfig.max_token_len);
    canvas.pushSprite(0, 0);
    llm_work_id = module_llm.llm.setup(llmConfig);

    canvas.printf("^ONI^ ok\nSay \"%s\" or press LISTEN\n", wake_up_keyword.c_str());
    canvas.pushSprite(0, 0);
    CoreS3.Speaker.setVolume(100);  // Set volume 
    CoreS3.Speaker.tone(262, 300);  // C4
    delay(200);                     // Slight pause between notes
    CoreS3.Speaker.tone(294, 300);  // D4
    delay(200);                     // Slight pause between notes
    CoreS3.Speaker.tone(262, 300);  // C4
    // setup basic touch zones last as they will not work until loop() is entered
    showSoftButtons(rootButtons);
}

String utterence = "";
String question = "";
String thisResponse="";
String lastQuestion= "";
String lastResponse= "";

// precompile the voice command regexp
#include <regex>
std::regex muteCommand(R"(.*mute go\.$)");
std::regex unmuteCommand(R"(.*unmute go\.$)");
std::regex batteryCommand(R"(.*battery charge go\.$)");
std::regex goCommand(R"(.* go\.$)");

// stats
int batteryState=0; // %
bool charging=false;
int USBVoltage=0; // in mV

// inference output state control
bool skipOutput=false;
// go mode causes the LLM to be invoked as soon as a full stop is seen. uttering go at the end of a sentence turns it on for one command
// go mode is intended where hands free is imperative
bool goMode=false;

void loop()
{
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
          Serial2.printf("{ \"request_id\":\"3\",\"work_id\":\"%s\",\"action\":\"work\" }", asr_work_id);
          // module_llm.asr.work(asr_work_id);
          delay(200);                     // Slight pause between notes
          CoreS3.Speaker.tone(294, 300);  // D4
          delay(200);                     // Slight pause between notes
          CoreS3.Speaker.tone(330, 300);  // E4
        }
        if((isButton2(touchPoint.x,touchPoint.y))&&(!goMode)){
          CoreS3.Speaker.tone(330, 300);  // E4 
          // STOP
          canvas.setTextColor(TFT_RED);
          canvas.println("\nStopping recognition and starting new chat");
          canvas.pushSprite(0, 0);
          utterence = "";
          question = "";
          lastQuestion= "";
          lastResponse= "";
          // TODO store old chat sessions
          // module_llm.asr.pause(asr_work_id); // stop any ASR
          Serial2.printf("{ \"request_id\":\"3\",\"work_id\":\"%s\",\"action\":\"pause\" }", asr_work_id);
          // CoreS3.Speaker.tone(330, 300);  // E4 
          delay(200);                     // Slight pause between notes
          CoreS3.Speaker.tone(294, 300);  // D4
          delay(200);                     // Slight pause between notes
          CoreS3.Speaker.tone(262, 300);  // C4
        }
        if((isButton3(touchPoint.x,touchPoint.y)) || goMode ){
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
          showSoftButtons(inferenceButtons);
          skipOutput=false;
          module_llm.llm.inferenceAndWaitResult(llm_work_id, fullQuestion.c_str(), [](String& result) {
            if(skipOutput) return; // fake stop just silently ingores the remaining results
            // Show result on screen
            outputCanvas.print(result.c_str());
            thisResponse+=result;
            outputCanvas.pushSprite(0, (CoreS3.Display.height() - 40)/3);
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
                return;
              } 
              if(isButton3(inferenceTouchPoint.x,inferenceTouchPoint.y)){
                // does nothing for now, reserved to "resume" is pause is implemented using pause/work
              }
            } 
            // TODO TTS
          });
          showSoftButtons(rootButtons);
          // TODO try proper memory strategies optimised for smaller context windows !
          lastResponse=thisResponse.substring(0, 200); // make sure the stored response it not too long
          outputCanvas.println("");
          outputCanvas.pushSprite(0, (CoreS3.Display.height() - 40)/3);
          // TODO revert button bar (move button bars into their own functions or class)
          CoreS3.Speaker.tone(262, 300);  // C4
        }
    }
   
    int state = CoreS3.BtnPWR.wasClicked();
    if (state) {
      // TODO 'sleep' if PWR pressed
      CoreS3.Speaker.tone(294, 300);  // D4
      canvas.setTextColor(TFT_ORANGE);
      canvas.println("ZZZ");
      canvas.pushSprite(0, 0);
      // "wind down tones"
      delay(200);                     // Slight pause between notes
      CoreS3.Speaker.tone(294, 300);  // D4
      delay(500);                     // longer pause between notes
      CoreS3.Speaker.tone(294, 300);  // D4
      delay(1000);                    // longer pause between notes
      CoreS3.Speaker.tone(294, 300);  // D4
    }

    /* Handle module response messages */
    for (auto& msg : module_llm.msg.responseMsgList) {
        /* If KWS module message */
        if (msg.work_id == kws_work_id) {
            canvas.setTextColor(TFT_GREEN);
            canvas.println("Listening!");
            canvas.pushSprite(0, 0);
            utterence = ""; // new utterence
            CoreS3.Speaker.tone(262, 500);  // C4
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
                deserializeJson(doc, msg.raw_msg);
                String lastUtterence = utterence; // will be "" if this is first packet
                utterence = doc["data"]["delta"].as<String>(); // stash in the global
                uint asr_index = doc["data"]["index"].as<uint>();

                outputCanvas.setTextColor(TFT_YELLOW);
                outputCanvas.printf("%s",(utterence.substring(lastUtterence.length())).c_str()); // print just the difference is any
                outputCanvas.pushSprite(0, (CoreS3.Display.height() - 40)/3);
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
}