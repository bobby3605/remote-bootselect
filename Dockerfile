FROM alpine:latest
RUN apk add gcc musl-dev linux-headers
WORKDIR /app
COPY src/server src/server
RUN gcc -O3 src/server/remote-bootselect-server.c src/server/util.c -o remote-bootselect-server

FROM alpine:latest
WORKDIR /app
COPY --from=0 /app/remote-bootselect-server .
COPY entrypoint.sh /app/
RUN chmod +x /app/entrypoint.sh
RUN adduser -S remote-bootselect
RUN apk add mosquitto-clients

ENTRYPOINT ["/app/entrypoint.sh"]
