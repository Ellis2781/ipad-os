FROM alpine:latest
RUN apk update
RUN apk add qemu make gcc bash

