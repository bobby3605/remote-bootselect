#!/usr/bin/env sh

config(){
    mosquitto_sub -h $MQTT_HOST -u $MQTT_USER -P $MQTT_PASS -t $MQTT_TOPIC > /tmp/remote-bootselect-server/config &
    mqtt_pid=$!
}
# wait for server to create config pipe before starting mqtt
trap config SIGUSR1
# kill all background processes when exiting script
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

/app/remote-bootselect-server -i $INTERFACE -l &
server_pid=$!

wait $server_pid
wait $mqtt_pid
