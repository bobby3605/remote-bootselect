FROM alpine:latest AS build

RUN apk add --no-cache \
    build-base \
    meson \
    ninja \
    mosquitto-dev \
    linux-headers

WORKDIR /app

COPY meson.build .
COPY src/server src/server
COPY external external

RUN meson setup build
RUN meson compile -C build

FROM alpine:latest

RUN apk add --no-cache mosquitto-libs gcc libcap

WORKDIR /app

COPY --from=build /app/build/remote-bootselect /app/remote-bootselect
RUN setcap cap_net_raw=ep /app/remote-bootselect

SHELL ["/bin/sh", "-c"]
ENTRYPOINT exec /app/remote-bootselect -i $INTERFACE -host $MQTT_HOST -port $MQTT_PORT -user $MQTT_USER -pass $MQTT_PASS
