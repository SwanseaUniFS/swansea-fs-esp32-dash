# swansea-fs-esp32-dash
Has code for the esp32 dash.

If you are trying to git clone, build and run the code for the first time, steps to get running are:
Check your file path does not have any spaces, if it does and it's your username you will have to make a new user otherwise change your file path.
make a github account.
download git and install.
make an ssh-key on your machine to allow pushing to your github account.
If you have not been added to the codebase you will need to request access to push.
once you have got git working, run git clone "repository" (to get repo on github press green button and make sure you use ssh not https)

Install VSCode
search for python in vscode extensions and install.
search for platformio in vscode extenstions and install.
you should restart vscode at the end of any major operation.

copy esp32-s3-devkitc-1-myboard.json located in RESOURCES into 
Windows C:\Users\<username>.\platformio\platforms\espressif32\boards
Linux ~/.platformio/platforms/espressif32/boards

run build, this will take over 10 minutes
if you have the screen, run upload. This will also take a long time the first time
