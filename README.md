# remote-bootselect
This program enables remotely setting the default boot option in grub.\
There is a server program and a grub module.\
This is currently alpha software and is not ready for real use.

## remote-bootselect-server
This program listens for broadcast packets with a custom ethertype.\
It must run as root because it operates on L2, so it reads and sends raw packets.\
Usage:
``` 
./remote-bootselect-server interface_name listen
```
It also has the capability to request default data. This is useful for debugging.
```
./remote-bootselect-server interface_name request
```
### Configuration:
Create a file named 'config' in the same directory as remote-bootselect-server.\
Add entries to the file following this example:
```
0a:1b:2c:3d:4e:5f default_entry
```
The first parameter is the mac address of the target server.\
The second parameter is what will be passed to the 'default' environment variable in grub.\
The second parameter can be up to 63 characters long.\
Create one line per config entry

## remote-bootselect.mod
This is the grub module that will communicate with the server and set the default entry.
### Installation:
``` 
cp remote-bootselect.mod /boot/grub/x86_64-efi/remote-bootselect.mod
cp src/grub/01-remote-bootselect file to /etc/grub.d/01-remote-bootselect
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
```
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
Currently the server closes after sending one packet.\
It should remain open, but also be able to be closed with signals.

The server should be able to be dynamically configured, both through a unix socket or by listening an MQTT topic.

Faster checking of entries. Currently, the entries are stored in a vector, which is O(n) to search.\
However, it should still take an insignificant time to search with any reasonable number of entries.\
Faster insertion of new entries as well once there is dynamic configuration.

The build process could be made simpler by only compiling the module instead of all of grub.

setuid() to a standard (installation created) user after opening the socket, to reduce the possible damage of a security vulnerability

Dockerfile for running the server\
Dockerfile for building the both the server and module

Debug log for server instead of stdout

Allow the network card/interface to be specified in the module

More robust checking of possible security vulnerabilities in the server or module code

General code cleanup 

Shared headers for ethertype and ethernet frames
