#!/usr/bin/env sh

/app/remote-bootselect-server -i $INTERFACE -l &
# wait for server to create config pipe before starting mqtt
sleep 1
mosquitto_sub -h $MQTT_HOST -u $MQTT_USER -P $MQTT_PASS -t $MQTT_TOPIC > /tmp/remote-bootselect-server/config &

wait -n

exit $?
