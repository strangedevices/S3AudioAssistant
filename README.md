**Oni AI Assistant Documentation**

This repository contains the Arduino code for **Oni**, an AI assistant built using the [M5Stack CoreS3](https://docs.m5stack.com/en/core/CoreS3) and [M5Stack Module LLM](https://docs.m5stack.com/en/module/Module-LLM). Oni is a voice-activated assistant supporting wake word detection, speech recognition, and language model inference with a touch-based interface.

**Overview**

Oni leverages the M5Stack CoreS3 for its display, touch input, and audio output, paired with the M5Stack Module LLM for AI capabilities, including Keyword Spotting (KWS), Automatic Speech Recognition (ASR), and Large Language Model (LLM) inference. The assistant supports voice commands, displays real-time status and output, and uses soft buttons for user interaction.

**Hardware Requirements**

* [M5Stack CoreS3](https://docs.m5stack.com/en/core/CoreS3)  
* [M5Stack Module LLM](https://docs.m5stack.com/en/module/Module-LLM)  
* Serial connection (configured for CoreS3: RXD pin 18, TXD pin 17\)

**Software Requirements**

* Arduino IDE or compatible environment  
* [M5Unified](https://github.com/m5stack/M5Unified) library  
* [M5CoreS3](https://github.com/m5stack/M5CoreS3) library  
* [M5ModuleLLM](https://github.com/m5stack/M5ModuleLLM) library  
* Model update for LLM Module (see here: [https://docs.m5stack.com/en/guide/llm/llm/image](https://docs.m5stack.com/en/guide/llm/llm/image) )

**Installation**

* **Set up the Arduino environment**:  
  * Install the [Arduino IDE](https://www.arduino.cc/en/software).  
  * Add M5Stack board definitions via Boards Manager (URL: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package\_m5stack\_index.json).  
  * Install required libraries (M5Unified, M5CoreS3, M5ModuleLLM) via the Library Manager.  
* **Clone this repository**:  
* bash  
* git clone https://github.com/strangedevices/S3AudioAssistant.git  
* **Upload the code**:  
  * Open the .ino file in the Arduino IDE.  
  * Connect the M5Stack CoreS3 via USB.  
  * Select the board (M5Stack CoreS3) and port, then upload the code.  
* **Configure the Module LLM**:  
  * Ensure the Module LLM is connected to the CoreS3 via serial pins (RXD: 18, TXD: 17).  
  * The code automatically initializes the module during setup.

**Usage**

**Interface**

* **Display**: The CoreS3 display is divided into three sections:  
  * **Status Canvas** (top): Shows system status (e.g., booting, listening).  
  * **Output Canvas** (middle): Displays speech recognition results and LLM responses.  
  * **Soft Buttons** (bottom): Touch zones for LISTEN, STOP\!, and GO (or PAUSE, STOP\!, .. during inference).  
* **Audio Feedback**: The speaker provides tonal feedback for actions (e.g., C4, D4, E4 notes for button presses).

**Major Voice Commands**

Oni recognizes specific voice commands, typically ending with go. to trigger actions. The wake word is ACKNOWLEDGE (case-sensitive, must be spoken clearly). Key commands include:

* **Wake Word**: ACKNOWLEDGE  
  * Activates the assistant and starts listening for further input.  
* **Mute**: mute go.  
  * Disables audio output (sets speaker volume to 0).  
* **Unmute**: unmute go.  
  * Re-enables audio output (restores speaker volume to 100).  
* **Battery Status**: battery charge go.  
  * Displays the current battery level (in percentage) on the status canvas.  
* **Go Mode**: \<any sentence\> go.  
  * Triggers immediate LLM inference without requiring a touch on the GO button. Useful for hands-free operation.

**Soft Button Controls**

* **Root Mode** (default):  
  * LISTEN: Starts speech recognition.  
  * STOP\!: Stops recognition and clears the current conversation.  
  * GO: Submits the recognized utterance to the LLM for inference.  
* **Inference Mode** (during LLM processing):  
  * PAUSE: Emits a tone (placeholder for future pause functionality).  
  * STOP\!: Halts LLM inference and discards remaining output.  
  * ..: Reserved for future functionality (e.g., resume).

**Example Interaction**

* Say ACKNOWLEDGE to wake Oni.  
* Speak a question, e.g., What is the capital of France go..  
* Oni recognizes the speech, displays it, and submits it to the LLM.  
* The LLM response (e.g., The capital of France is Paris.) appears on the output canvas.  
* Use soft buttons or voice commands (mute go., etc.) to control Oni.

**Code Structure**

* **Libraries**:  
  * M5Unified: Core library for M5Stack hardware.  
  * M5CoreS3: Specific support for CoreS3 features (display, touch, speaker).  
  * M5ModuleLLM: Interface for the Module LLM (KWS, ASR, LLM).  
* **Global Variables**:  
  * module\_llm: Instance of the Module LLM.  
  * wake\_up\_keyword: Set to ACKNOWLEDGE for KWS.  
  * canvas and outputCanvas: Display canvases for status and output.  
  * utterence, question, lastResponse: Track speech and LLM context.  
* **Setup**:  
  * Initializes CoreS3, display, speaker, and Module LLM.  
  * Configures KWS (ACKNOWLEDGE), ASR, and LLM (default model: qwen2.5-1.5B-ax630c).  
  * Sets up touch zones and soft buttons.  
* **Loop**:  
  * Updates hardware and module states.  
  * Handles touch input for soft buttons.  
  * Processes Module LLM responses (KWS for wake word, ASR for speech).  
  * Executes voice commands via regex matching.  
  * Manages LLM inference and output display.

**TODO List**

* **Text-to-Speech (TTS) Integration**:  
  * Add TTS to vocalize LLM responses using the CoreS3 speaker.  
  * Support language selection (en\_US, zh\_CN) aligned with the language setting.  
  * Handle mute/unmute states to disable/enable TTS output.  
* **Local Wikipedia VIM Archive Integration**:  
  * Implement a local Wikipedia archive (e.g., compressed VIM format) for offline knowledge retrieval.  
  * Add voice commands to query the archive (e.g., wiki \<topic\> go.).  
  * Integrate results into the LLM prompt for enhanced responses.  
* **Model Switching via Voice Command**:  
  * Enable switching between LLM models (e.g., think fast go., think well go.) using voice commands.  
  * Update the canvas to reflect the active model.  
* **Improved Pause/Resume for LLM Inference**:  
  * Implement proper pause/resume functionality for LLM inference using pause and work actions.  
  * Update the inference mode soft buttons to support resuming.  
* **Chat History Storage**:  
  * Store previous questions and responses in flash memory for session persistence.  
  * Allow retrieval of past conversations via voice or touch commands.  
* **Sleep Mode**:  
  * Implement a low-power sleep mode triggered by the power button.  
  * Add wake-up via touch or wake word.  
* **Optimize Context Management**:  
  * Develop memory strategies for smaller LLM context windows to improve response coherence.  
  * Trim or summarize lastResponse to fit within token limits.

**Notes**

* **Wake Word Sensitivity**: The KWS module is highly sensitive to the wake word (ACKNOWLEDGE). Avoid short or common words to prevent false positives.  
* **LLM Models**: The default model is qwen2.5-1.5B-ax630c. Other models (e.g., qwen2.5-0.5B-prefill-20e, deepseek-r1-1.5B-ax630c) are available but require manual configuration. See [StackFlow models](https://github.com/m5stack/StackFlow/tree/dev/projects/llm_framework/main_llm/models) for details.  
* **Voice Command Regex**: Commands like mute go. are matched using regex for flexibility. Ensure utterances end with go. for reliable detection.  
* **License**: The code is available for personal experimentation. For commercial use, contact oni@mashplex.com.

**Contributing**

Contributions are welcome\! Please submit issues or pull requests for bug fixes, feature enhancements, or documentation improvements. Ensure changes are tested on the M5Stack CoreS3 and Module LLM hardware.

**License**

Copyright © 2025 Jeremy Kelaher. All rights reserved. Licensed for personal experimentation only. For commercial use, contact oni@mashplex.com.

---

