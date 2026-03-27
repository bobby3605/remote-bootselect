# remote-bootselect
This project enables remotely setting the default boot option in grub.\
There is a server program and a grub module.

# Quickstart
## Server
docker-compose.yml:
```
services:
  remote-bootselect:
    image: bobby3605/remote-bootselect:latest
    network_mode: "host"
    cap_add:
      - NET_RAW
    environment:
      INTERFACE: ""
      MQTT_HOST: ""
      MQTT_PORT: ""
      MQTT_USER: ""
      MQTT_PASS: ""
```
## Home Assistant
Enable MQTT in home assistant with auto discovery on the homeassistant/ topic
## GRUB module
Download the latest grub module from the releases.\
Currently, it is only built for x86_64-efi.\
Follow the [remote-bootselect.mod installation section](#Installation).

# Documentation

## remote-bootselect
This program listens for broadcast packets with a custom ethertype (0x7184).\
It needs NET_RAW and host networking due to opening a raw L2 socket.
Usage:
``` 
./remote-bootselect -i interface_name -host mqtt_host -port mqtt_port -user mqtt_user -pass mqtt_pass
```
### Configuration:
You can pass a config file to remote-bootselect-server with the '-c' flag.\
Add entries to the file following this example:
```
0a:1b:2c:3d:4e:5f default_entry
```
The first parameter is the mac address of the target server.\
The second parameter is what will be passed to the 'default' environment variable in grub (usually the id of an entry).\
The second parameter can be up to 255 characters long.\
Create one line per config entry.\
This same file format can also be sent to the /tmp/remote-bootselect.sock unix socket.\
This allows for dynamically changing the default entry of a server.
## remote-bootselect.mod
This is the grub module that will communicate with the server and set the default entry.
### Installation:
Install the module into your grub boot folder:
``` 
cp remote-bootselect.mod /boot/grub/x86_64-efi/remote-bootselect.mod
```
Copy the ```00-remote_bootselect``` and ```99-remote_bootselect_export``` files to /etc/grub.d/
```
cp src/grub/00-remote_bootselect /etc/grub.d/
cp src/grub/99-remote_bootselect /etc/grub.d/
```
update grub
```
grub-mkconfig -o /boot/grub/grub.cfg
```
## Building:
### remote-bootselect
```
meson setup build
meson compile -C build .
sudo setcap cap_net_raw=ep build/remote-bootselect
```
### remote_bootselect.mod:
Ensure you have the grub source:
```
git submodule update --init --recursive
```
Create the build container image:
```
docker compose -f docker-compose-build.yml up -d --build
```
Run the build:
```
docker compose -f docker-compose-build.yml exec remote-bootselect-build make
```
./container-build now contains remote_bootselect.mod
If you want to change the platform architecture,\
edit the configure-grub section in 'Makefile' to your platform.\
Then run the above the container build and source build steps.

## Protocol:
### Server:
#### Request:
The server will listen for packets with ethertype 0x7184.\
A request packet has no data attached, so it is only a destination, source, and ethertype.\
The server will respond to the request with the following packet:
```
destination|source|ethertype|data
request_source_mac|server_mac|ethertype|entry_length|default_entry
```
#### Export:
The server will listen for packets on the ethertype which have data attached
```
destination|source|ethertype|data
request_source_mac|server_mac|ethertype|char* entries[]
```
The server will build the MQTT auto discovery and send it to the MQTT server
### Client:
#### Request:
The client will send a request packet as described in the Server section to the broadcast mac address (ff:ff:ff:ff:ff:ff)\
The client will wait for packets with the destination as its mac address and the correct ethertype\
Once the client receives and verifies the packet, it will set the default entry and exit
#### Export:
The client sends grub menu entry data in the packet format specified above
This can only be sent after grub has loaded the menu entries

## TODO:
Simple building for other platforms

The build process could be made simpler by only compiling the module instead of all of grub.

More robust checking of possible security vulnerabilities in the server or module code.

General code cleanup.

Shared headers for ethertype and ethernet frames between server and module.

