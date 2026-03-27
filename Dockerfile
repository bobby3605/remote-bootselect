FROM alpine:latest
RUN apk add meson gcc musl-dev linux-headers mosquitto
WORKDIR /app
COPY src/server src/server
COPY external external

FROM alpine:latest
WORKDIR /app
COPY --from=0 /app/remote-bootselect-server .

ENTRYPOINT ["/app/remote-bootselect -i $INTERFACE -host $MQTT_HOST -port $MQTT_PORT -user $MQTT_USER -pass $MQTT_PASS"]
