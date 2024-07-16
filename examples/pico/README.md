## Setup

```
~/build$ export PICO_SDK_PATH=/home/your-username/src/pico-sdk
~/pico-examples/build$ cmake -DPICO_BOARD=pico_w -DWIFI_SSID="your-network-name" -DWIFI_PASSWORD="your-network-password" ..
~/pico-examples/build$ make -j8
```

## Secure Network Credentials

###  Create `credentials.h` for the client

Create a file named `credentials.h` to store the Wi-Fi SSID and password. This file should be included in the `.gitignore` file to prevent it from being tracked by Git.

```c
// credentials.h
#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#define WIFI_SSID "Your_SSID"
#define WIFI_PASSWORD "Your_Password"

#endif // CREDENTIALS_H
```

### Check prints

```
~$ minicom -b 115200 -o -D /dev/ttyACM0
```