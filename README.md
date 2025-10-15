# ESP32 Dash Code

Holds the code to initialise the ESP32 Dash using Platformio.
---

## Installation

1. Download [**VSCode**](https://code.visualstudio.com/download) (Or Different IDE) 
2. Create a new folder on your computer and open in **VSCode**
3. Ensure the file path has no spaces. If it does, rename the folders before **UNLESS** it is your user, you will have to make a [New User](https://support.microsoft.com/en-gb/windows/manage-user-accounts-in-windows-104dc19f-6430-4b49-6a2b-e4dbd1dcdf32) on your computer to download the file.
4. Ensure [Git](https://git-scm.com/downloads) is installed on your machine
5. In **VSCode**, press `Ctrl+Shift+'` to open the terminal or Press `terminal` -> `New Terminal`
6. Type `git clone https://github.com/SwanseaUniFS/swansea-fs-esp32-dash` in the Terminal and press `Enter`
7. Open Extensions in VSCode `Ctrl+Shift+X` or on the left bar
8. Search for and Install `Python` and `Platformio`
9. Restart **VSCode**
10. Copy `esp32-s3-devkitc-1-myboard.json` from the `RESOURCES` folder into `C:\Users\<username>.\platformio\platforms\espressif32\boards` on **Windows**, `~/.platformio/platforms/espressif32/boards` on **Linux** 
11. Press the Tick to build, this may take a long time (~10 Minutes)
    
    <img width="733" height="182" alt="image" src="https://github.com/user-attachments/assets/2046deda-909c-4f4a-a59f-d3690f1d3e90" />


    
13. Plug the screen into your computer via USB, press the arrow to upload
    
    <img width="733" height="188" alt="image" src="https://github.com/user-attachments/assets/fcc67ed0-491a-4e67-9276-9402f80b766e" />
    

This will add the UI onto the screen.

---

## Converting to Headless Mode

1. Unplug the 4 Pin JST Connector for the ESP32 Screen Module
   
   <img width="521" height="521" alt="image" src="https://github.com/user-attachments/assets/217d4a04-f396-4bb4-be5c-fb3ca4b260d0" />
3. Plug 4 Male to Female plugs into the 4 holes of the JST Connector
4. From Left to Right on the JST, on the ESP-S3-DEV-KIT, the 1st connector goes to GND, 2nd goes to 3V3, 3rd to Pin 4 (TX) and 4th to Pin 5 (RX)
5. In the code, change the line `HAS_DISPLAY 1` to `HAS_DISPLAY 0` so the information will get sent to the serial monitor
6. Ensure the serial speed matches the serial monitor (usually 9600)
7. Unplug the CANBUS Connector from your ESP32-S3 and ensure the COM is set to the correct port
8. Upload the code to the ESP32-S3 (**Step 13 of Installation**)
9. Open the Serial Monitor
    
   <img width="266" height="32" alt="image" src="https://github.com/user-attachments/assets/4b883b25-4544-479a-8f16-20b6189bcf22" />
11. Replug in the CANBUS and ensure the data is getting transmitted to the serial monitor every time it is recieved from the Pico or the ECU 


