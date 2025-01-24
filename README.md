# remote-bootselect
This program enables remotely setting the default boot option in grub.\
There is a server program and a grub module.

# Quickstart
## Server
Pull the server container and run it:
```
docker pull bobby3605/remote-bootselect
```
Copy docker-compose.yml.\
Edit the environment variables for the interface and MQTT info.\
Run the container as root:
```
sudo docker compose up -d
```
## Home Assistant
Edit the home assistant auto discovery payload shown in the [Home Assistant configuration section](#home-assistant-1).\
Minify it at https://jsonformatter.org/ \
Submit it to the home assistant discovery topic, as shown in the [Home Assistant configuration section](#home-assistant-1).
## GRUB module
Download the latest grub module from the releases.\
Currently, it is only built for x86_64-efi.\
Follow the [remote-bootselect.mod installation section](#installation).

# Documentation

## remote-bootselect-server
This program listens for broadcast packets with a custom ethertype.\
It must run as root because it operates on L2, so it reads and sends raw packets.\
It will drop its permissions to the remote-bootselect user after opening the raw socket.\
Usage:
``` 
./remote-bootselect-server interface_name listen
```
It also has the capability to request default data. This is useful for debugging.
```
./remote-bootselect-server interface_name request
```
### Configuration:
You can pass a config file to remote-bootselect-server with the '-c' flag
Add entries to the file following this example:
```
0a:1b:2c:3d:4e:5f default_entry
```
The first parameter is the mac address of the target server.\
The second parameter is what will be passed to the 'default' environment variable in grub.\
The second parameter can be up to 63 characters long.\
Create one line per config entry
This same file format can also be sent to the /tmp/remote-bootselect-server/config pipe.
This allows for dynamically changing the default entry of a server.
#### MQTT:
The config pipe allows for easy integration with MQTT:\
Subscribe to a topic and output to the pipe:
```
mosquitto_sub -h MQTT_HOSTNAME -u MQTT_USER -P MQTT_PASSWORD -t remote-bootselect > /tmp/remote-bootselect-server/config
```
Publish default entry data to the topic in retained mode:
```
mosquitto_pub -h MQTT_HOSTNAME -u MQTT_USER -P MQTT_PASSWORD -t remote-bootselect -r -m "aa:bb:cc:dd:ee:ff 1"
```
#### Home Assistant:
An auto discovery payload can be sent to the home assistant discovery topic in MQTT to add buttons for remote-bootselect in home assistant.\
Example MQTT auto discovery:
```
{
  "dev": {
    "ids": "remote-bootselect",
    "name": "Remote Bootselect"
  },
  "o": {
    "name":"remote-bootselect",
    "url": "https://github.com/bobby3605/remote-bootselect/"
  },
  "cmps": {
    "boot_0": {
      "p": "button",
      "unique_id":"boot_0",
      "name":"Linux boot option",
      "command_topic": "remote-bootselect",
      "payload_press": "aa:bb:cc:dd:ee:ff 0",
      "retain": true
    },
    "boot_1": {
      "p": "button",
      "unique_id":"boot_1",
      "name":"Windows boot option",
      "command_topic": "remote-bootselect",
      "payload_press": "aa:bb:cc:dd:ee:ff 1",
      "retain": true
    }
  },
  "qos": 0
}
```
Publish to home assistant discovery topic with retained mode:
```
mosquitto_pub -h MQTT_HOSTNAME -u MQTT_USER -P MQTT_PASSWORD -t homeassistant/device/remote-bootselect/config -r -m '{"dev":{"ids":"remote-bootselect","name":"Remote Bootselect"},"o":{"name":"remote-bootselect","url":"https://github.com/bobby3605/remote-bootselect/"},"cmps":{"boot_0":{"p":"button","unique_id":"boot_0","name":"Linux boot option","command_topic":"remote-bootselect","payload_press":"aa:bb:cc:dd:ee:ff 0","retain":true},"boot_1":{"p":"button","unique_id":"boot_1","name":"Windows boot option","command_topic":"remote-bootselect","payload_press":"aa:bb:cc:dd:ee:ff 1","retain":true}},"qos":0}'
```
Note:\
Attempting to configure the boot options of multiple devices can be a bit tricky,\
because the whole configuration of all devices needs to be in the payload.\
Or, when the servers starts up, you can individually send the payload messages for each server.

## remote-bootselect.mod
This is the grub module that will communicate with the server and set the default entry.
### Installation:
Copy src/grub/01-remote-bootselect to /etc/grub.d/01-remote-bootselect
``` 
cp remote-bootselect.mod /boot/grub/x86_64-efi/remote-bootselect.mod
chmod +x /etc/grub.d/01-remote-bootselect
update-grub
```

## Building:
To build everything, follow the instructions to bootstrap and configure grub in the remote-bootselect.mod section,
then run:
```
make
```
This will build both the server and module and place them in build/
### Dependencies:
``` 
gcc pkg-config m4 libtool automake autoconf bison flex
```
### remote-bootselect-server
```
make build/remote-bootselect-server
make user
```
'make user' ensures that the remote-bootselect system user exists.
### remote-bootselect.mod
Ensure you have the grub source:
```
git submodule update --init --recursive
```
Add remote-bootselect.mod to the grub build config and bootstrap grub:
```
make bootstrap_grub
```
Configure grub - if you want to change the target architecture, you can edit this section in the makefile:
```
make configure_grub
```
Build the module:
```
make build/remote-bootselect.mod
```
Building will build all of grub (by default using all cores, edit the makefile to change this),
so it may take some time to build depending on your system
The final built grub module will be located here:
```
grub/grub-core/remote-bootselect.mod
```
Add the module to your grub boot folder
```
cp grub/grub-core/remote-bootselect.mod /boot/grub/x86_64-efi/remote-bootselect.mod
```

## Protocol:
### Server:
The server will listen for packets with ethertype 0x7184 (defined in src/server/remote-bootselect-server.h).\
A request packet has no data attached, so it is only a destination, source, and ethertype.\
The server will respond to the request with the following packet:
```
destination|source|ethertype|data
request_source_mac|server_mac|ethertype|default_entry
```
### Client:
The client will send a request packet as described in the Server section to the broadcast mac address (ff:ff:ff:ff:ff:ff)\
The client will wait for packets with the destination as its mac address and the correct ethertype\
Once the client receives and verifies the packet, it will set the default entry and exit

## TODO:
Safely close the server by handling signals

When the program that writes to the config pipe closes,\
epoll_wait immediately returns EPOLLHUP on every call,\
resulting in high cpu usage.\
It can still read from the pipe if a process writes to it,\
but now epoll_wait() returns EPOLLHUP immediately if there's no data.

Better handling of multiple devices in MQTT and home assistant

Faster checking of entries. Currently, the entries are stored in a vector, which is O(n) to search.\
However, it should still take an insignificant time to search with any reasonable number of entries.\
Faster insertion of new entries as well once there is dynamic configuration.

The build process could be made simpler by only compiling the module instead of all of grub.

Dockerfile for building the both the server and module.\
Send signal when server is done initializing.\
This could allow the container to wait on the signal instead of 'sleep 1'

Properly handle kill signals in docker entrypoint.\
Currently, the container stops itself after 10 seconds.\
It should be quick to stop.

Allow the network card/interface to be specified in the module

More robust checking of possible security vulnerabilities in the server or module code

General code cleanup 

Shared headers for ethertype and ethernet frames between server and module

